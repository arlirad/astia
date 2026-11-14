#include <astia/module_abi.h>
#include "virtio_blk_protocol.hpp"

namespace {

    constexpr unsigned page_size = 4096;
    constexpr unsigned queue_limit = 8;
    constexpr unsigned timeout_spins = 4000000;
    constexpr unsigned pci_vendor_virtio = 0x1af4;
    constexpr unsigned pci_device_blk = 0x1042;
    constexpr unsigned virtio_capability = 0x09;
    constexpr unsigned cap_common = 1;
    constexpr unsigned cap_notify = 2;
    constexpr unsigned cap_device = 4;
    constexpr unsigned feature_version_1 = 32;
    constexpr unsigned feature_ring_reset = 40;
    constexpr unsigned feature_blk_ro = 5;
    constexpr unsigned feature_blk_flush = 9;
    constexpr unsigned request_in = 0;
    constexpr unsigned request_out = 1;
    constexpr unsigned request_flush = 4;
    constexpr unsigned status_ok = 0;
    constexpr unsigned status_ioerr = 1;
    constexpr unsigned status_unsupp = 2;
    constexpr unsigned status_acknowledge = 1;
    constexpr unsigned status_driver = 2;
    constexpr unsigned status_features_ok = 8;
    constexpr unsigned status_driver_ok = 4;
    constexpr unsigned descriptor_next = 1;
    constexpr unsigned descriptor_write = 2;

    struct dma {
        astia_module_dma_buffer value{};
        bool allocated = false;

        bool allocate(unsigned pages) {
            allocated = astia_module_dma_alloc(pages, 1, &value) == 0;
            return allocated;
        }

        void release() {
            if (!allocated)
                return;
            astia_module_dma_release(&value);
            allocated = false;
        }
    };

    struct capability_region {
        bool present = false;
        unsigned char bar = 0;
        uint64_t offset = 0;
        uint64_t length = 0;
    };

    struct descriptor {
        uint64_t address;
        uint32_t length;
        uint16_t flags;
        uint16_t next;
    };

    struct available_ring {
        uint16_t flags;
        uint16_t index;
        uint16_t ring[queue_limit];
    };

    struct used_element {
        uint32_t id;
        uint32_t length;
    };

    struct used_ring {
        uint16_t flags;
        uint16_t index;
        used_element ring[queue_limit];
    };

    struct queue {
        dma memory;
        unsigned size = 0;
        unsigned descriptor_offset = 0;
        unsigned available_offset = 0;
        unsigned used_offset = 0;
        uint16_t available_index = 0;
        uint16_t used_index = 0;
        uint16_t notify_offset = 0;
        bool enabled = false;

        descriptor* descriptors() const {
            return reinterpret_cast<descriptor*>(
                static_cast<unsigned char*>(memory.value.virtual_address) + descriptor_offset);
        }

        available_ring* available() const {
            return reinterpret_cast<available_ring*>(
                static_cast<unsigned char*>(memory.value.virtual_address) + available_offset);
        }

        volatile used_ring* used() const {
            return reinterpret_cast<volatile used_ring*>(
                static_cast<unsigned char*>(memory.value.virtual_address) + used_offset);
        }

        void clear() {
            auto* bytes = static_cast<unsigned char*>(memory.value.virtual_address);
            for (unsigned i = 0; i < page_size; ++i)
                bytes[i] = 0;
            available_index = 0;
            used_index = 0;
        }
    };

    void memory_barrier() {
        asm volatile("mfence" ::: "memory");
    }

    uint16_t read16(volatile unsigned char* address, uint64_t offset) {
        return *reinterpret_cast<volatile uint16_t*>(address + offset);
    }

    uint32_t read32(volatile unsigned char* address, uint64_t offset) {
        return *reinterpret_cast<volatile uint32_t*>(address + offset);
    }

    uint64_t read64(volatile unsigned char* address, uint64_t offset) {
        return static_cast<uint64_t>(read32(address, offset)) |
               (static_cast<uint64_t>(read32(address, offset + 4)) << 32);
    }

    void write16(volatile unsigned char* address, uint64_t offset, uint16_t value) {
        *reinterpret_cast<volatile uint16_t*>(address + offset) = value;
    }

    void write32(volatile unsigned char* address, uint64_t offset, uint32_t value) {
        *reinterpret_cast<volatile uint32_t*>(address + offset) = value;
    }

    void write64(volatile unsigned char* address, uint64_t offset, uint64_t value) {
        write32(address, offset, static_cast<uint32_t>(value));
        write32(address, offset + 4, static_cast<uint32_t>(value >> 32));
    }

    uint8_t pci_byte(void* pci, uint16_t offset) {
        const uint32_t value = astia_module_pci_read32(pci, static_cast<uint16_t>(offset & ~3u));
        return static_cast<uint8_t>(value >> ((offset & 3u) * 8u));
    }

    uint32_t pci_dword(void* pci, uint16_t offset) {
        return astia_module_pci_read32(pci, static_cast<uint16_t>(offset & ~3u));
    }

    struct controller {
        void* pci = nullptr;
        volatile unsigned char* common = nullptr;
        volatile unsigned char* notify = nullptr;
        volatile unsigned char* device_config = nullptr;
        capability_region common_cap{};
        capability_region notify_cap{};
        capability_region device_cap{};
        uint32_t device_features_low = 0;
        uint32_t device_features_high = 0;
        uint64_t capacity = 0;
        uint32_t block_size = 512;
        uint32_t notify_multiplier = 0;
        bool ring_reset = false;
        bool flush_supported = false;
        bool write_supported = false;
        bool started = false;
        bool failed = false;
        queue request_queue{};

        bool region_valid(const capability_region& region, unsigned required) const {
            if (!region.present || region.bar >= 6 || region.offset > ~uint64_t{0} - required ||
                region.offset + required > astia_module_pci_bar_size(pci, region.bar))
                return false;
            return true;
        }

        bool map_region(const capability_region& region, volatile unsigned char*& result) {
            if (!region.present || region.bar >= 6 || region.offset > ~uint64_t{0} - region.length ||
                region.offset + region.length > astia_module_pci_bar_size(pci, region.bar))
                return false;
            uint64_t mapped = astia_module_pci_bar_mapped(pci, region.bar);
            if (mapped == 0) {
                if (astia_module_pci_map_bar(pci, region.bar) != 0)
                    return false;
                mapped = astia_module_pci_bar_mapped(pci, region.bar);
            }
            if (mapped == 0)
                return false;
            result = reinterpret_cast<volatile unsigned char*>(mapped + region.offset);
            return true;
        }

        bool find_capabilities() {
            common_cap = {};
            notify_cap = {};
            device_cap = {};
            const uint8_t pointer = pci_byte(pci, 0x34);
            if (pointer == 0 || pointer == 0xff)
                return false;
            bool visited[256]{};
            uint8_t current = pointer;
            for (unsigned step = 0; step < 48 && current != 0; ++step) {
                if (current < 0x40 || current > 0xfc || (current & 3u) != 0 || visited[current])
                    return false;
                visited[current] = true;
                const uint8_t id = pci_byte(pci, current);
                const uint8_t next = pci_byte(pci, static_cast<uint16_t>(current + 1));
                const uint8_t length = pci_byte(pci, static_cast<uint16_t>(current + 2));
                if (id == virtio_capability && length >= 16) {
                    const uint8_t type = pci_byte(pci, static_cast<uint16_t>(current + 3));
                    const uint8_t bar = pci_byte(pci, static_cast<uint16_t>(current + 4));
                    const uint64_t offset =
                        pci_dword(pci, static_cast<uint16_t>(current + 8));
                    const uint64_t region_length =
                        pci_dword(pci, static_cast<uint16_t>(current + 12));
                    capability_region* destination = type == cap_common
                                                         ? &common_cap
                                                         : type == cap_notify ? &notify_cap
                                                                              : type == cap_device
                                                                                    ? &device_cap
                                                                                    : nullptr;
                    if (destination != nullptr && !destination->present) {
                        destination->present = true;
                        destination->bar = bar;
                        destination->offset = offset;
                        destination->length = region_length;
                        if (type == cap_notify) {
                            if (length < 20)
                                return false;
                            notify_multiplier =
                                pci_dword(pci, static_cast<uint16_t>(current + 16));
                        }
                    }
                }
                current = next;
            }
            return common_cap.present && notify_cap.present && device_cap.present;
        }

        bool wait_status(uint8_t expected) const {
            for (unsigned spin = 0; spin < timeout_spins; ++spin) {
                if (*reinterpret_cast<volatile uint8_t*>(common + 0x14) == expected)
                    return true;
                asm volatile("pause");
            }
            return false;
        }

        void set_status(uint8_t value) {
            *reinterpret_cast<volatile uint8_t*>(common + 0x14) = value;
        }

        bool reset_device() {
            set_status(0);
            return wait_status(0);
        }

        bool negotiate_features() {
            write32(common, 0x00, 0);
            device_features_low = read32(common, 0x04);
            write32(common, 0x00, 1);
            device_features_high = read32(common, 0x04);
            if ((device_features_high & (1u << (feature_version_1 - 32))) == 0)
                return false;
            const uint32_t accepted_low = device_features_low &
                                          ((1u << feature_blk_ro) | (1u << feature_blk_flush));
            const uint32_t accepted_high =
                device_features_high & (1u << (feature_version_1 - 32));
            ring_reset = (device_features_high & (1u << (feature_ring_reset - 32))) != 0 &&
                         common_cap.length >= 0x3a;
            flush_supported = (device_features_low & (1u << feature_blk_flush)) != 0;
            write_supported = flush_supported &&
                              (device_features_low & (1u << feature_blk_ro)) == 0;
            write32(common, 0x08, 0);
            write32(common, 0x0c, accepted_low);
            write32(common, 0x08, 1);
            write32(common, 0x0c, accepted_high);
            set_status(status_acknowledge | status_driver | status_features_ok);
            const uint8_t status = *reinterpret_cast<volatile uint8_t*>(common + 0x14);
            return (status & status_features_ok) != 0;
        }

        bool read_configuration() {
            if (!region_valid(device_cap, 8))
                return false;
            for (unsigned attempt = 0; attempt < 16; ++attempt) {
                const uint8_t before = *reinterpret_cast<volatile uint8_t*>(common + 0x15);
                const uint64_t sectors = read64(device_config, 0);
                const uint8_t after = *reinterpret_cast<volatile uint8_t*>(common + 0x15);
                if (before != after)
                    continue;
                if (sectors == 0 || sectors > (~uint64_t{0} / 512))
                    return false;
                capacity = sectors;
                return true;
            }
            return false;
        }

        bool configure_queue() {
            write16(common, 0x16, 0);
            const unsigned maximum = read16(common, 0x18);
            if (maximum == 0)
                return false;
            unsigned size = maximum < queue_limit ? maximum : queue_limit;
            while (size != 0 && (size & (size - 1u)) != 0)
                size &= size - 1u;
            if (size < 4)
                return false;
            request_queue.size = size;
            request_queue.descriptor_offset = 0;
            request_queue.available_offset = size * 16;
            request_queue.used_offset = (request_queue.available_offset + 6 + size * 2 + 3) & ~3u;
            if (request_queue.used_offset + 6 + size * 8 > page_size ||
                !request_queue.memory.allocate(1))
                return false;
            request_queue.clear();
            write16(common, 0x18, static_cast<uint16_t>(size));
            write16(common, 0x1a, 0xffff);
            write64(common, 0x20, request_queue.memory.value.physical + request_queue.descriptor_offset);
            write64(common, 0x28, request_queue.memory.value.physical + request_queue.available_offset);
            write64(common, 0x30, request_queue.memory.value.physical + request_queue.used_offset);
            request_queue.notify_offset = read16(common, 0x1e);
            const uint64_t notify_offset =
                static_cast<uint64_t>(request_queue.notify_offset) * notify_multiplier;
            if (notify_offset > notify_cap.length || notify_offset + 2 > notify_cap.length) {
                request_queue.memory.release();
                return false;
            }
            write16(common, 0x1c, 1);
            if (read16(common, 0x1c) != 1) {
                request_queue.memory.release();
                return false;
            }
            request_queue.enabled = true;
            return true;
        }

        void disable_queue() {
            if (!request_queue.enabled)
                return;
            if (ring_reset) {
                write16(common, 0x38, 1);
                for (unsigned spin = 0; spin < timeout_spins; ++spin) {
                    if (read16(common, 0x38) == 0)
                        break;
                    asm volatile("pause");
                }
            }
            request_queue.enabled = false;
        }

        void release() {
            disable_queue();
            if (common != nullptr)
                (void) reset_device();
            request_queue.memory.release();
            common = nullptr;
            notify = nullptr;
            device_config = nullptr;
            started = false;
        }

        bool start() {
            if (astia_module_pci_enable_bus_master(pci) != 0 ||
                astia_module_pci_disable_msix(pci) != 0 || !find_capabilities())
                return false;
            if (!map_region(common_cap, common) || !map_region(notify_cap, notify) ||
                !map_region(device_cap, device_config) || common_cap.length < 0x38 ||
                notify_multiplier == ~uint32_t{0})
                return false;
            if (!reset_device())
                return false;
            set_status(status_acknowledge | status_driver);
            if (!negotiate_features() || !read_configuration())
                return false;
            if (!configure_queue())
                return false;
            set_status(status_acknowledge | status_driver | status_features_ok | status_driver_ok);
            if ((*reinterpret_cast<volatile uint8_t*>(common + 0x14) & status_driver_ok) == 0)
                return false;
            started = true;
            astia_module_log_u64("vblk", "sectors", capacity);
            return true;
        }

        bool reset_queue_after_timeout() {
            if (!ring_reset)
                return false;
            disable_queue();
            request_queue.clear();
            write16(common, 0x18, static_cast<uint16_t>(request_queue.size));
            write64(common, 0x20, request_queue.memory.value.physical + request_queue.descriptor_offset);
            write64(common, 0x28, request_queue.memory.value.physical + request_queue.available_offset);
            write64(common, 0x30, request_queue.memory.value.physical + request_queue.used_offset);
            write16(common, 0x1c, 1);
            if (read16(common, 0x1c) != 1)
                return false;
            request_queue.enabled = true;
            return true;
        }

        struct pending_request {
            dma request{};
            astia_module_io_request* io = nullptr;
            unsigned length = 0;
            unsigned data_offset = 64;
            unsigned status_offset = 0;
            bool write = false;
            bool flush = false;
            bool canceled = false;
            bool active = false;
        } pending{};

        bool valid_transfer(unsigned length, uint64_t offset, bool write) const {
            if (!started || failed || length > 8192 ||
                !astia_virtio_blk::valid_range(capacity, length, offset))
                return false;
            return !write || write_supported;
        }

        bool enqueue(dma& request, unsigned data_offset, unsigned data_length, bool write,
                     unsigned status_offset, unsigned type, uint64_t offset) {
            auto* bytes = static_cast<unsigned char*>(request.value.virtual_address);
            auto* header = reinterpret_cast<uint32_t*>(bytes);
            header[0] = type;
            header[1] = 0;
            reinterpret_cast<uint64_t*>(bytes + 8)[0] = type == request_flush ? 0 : offset / 512;
            bytes[status_offset] = 0xff;
            descriptor* descriptors = request_queue.descriptors();
            descriptors[0] = {request.value.physical, 16, descriptor_next, 1};
            if (data_length != 0) {
                descriptors[1] = {request.value.physical + data_offset, data_length,
                                  static_cast<uint16_t>(descriptor_next |
                                                        (write ? 0 : descriptor_write)), 2};
                descriptors[2] = {request.value.physical + status_offset, 1, descriptor_write, 0};
            }
            else {
                descriptors[1] = {request.value.physical + status_offset, 1, descriptor_write, 0};
            }
            available_ring* available = request_queue.available();
            available->ring[request_queue.available_index % request_queue.size] = 0;
            memory_barrier();
            ++request_queue.available_index;
            available->index = request_queue.available_index;
            memory_barrier();
            const uint64_t notify_offset =
                static_cast<uint64_t>(request_queue.notify_offset) * notify_multiplier;
            if (notify_offset + 2 > notify_cap.length)
                return false;
            write16(notify, notify_offset, 0);
            return true;
        }

        long wait(dma& request, unsigned data_length, unsigned status_offset,
                  void* buffer, bool write) {
            volatile used_ring* used = request_queue.used();
            bool completed = false;
            bool observed = false;
            for (unsigned spin = 0; spin < timeout_spins; ++spin) {
                memory_barrier();
                if (used->index != request_queue.used_index) {
                    const volatile used_element& element =
                        used->ring[request_queue.used_index % request_queue.size];
                    ++request_queue.used_index;
                    observed = true;
                    completed = element.id == 0 &&
                               astia_virtio_blk::completion_length_ok(
                                   write ? request_out : request_in, data_length, element.length);
                    break;
                }
                asm volatile("pause");
            }
            if (!completed) {
                if (!observed && !reset_queue_after_timeout())
                    failed = true;
                return observed ? -5 : -110;
            }
            const uint8_t status = static_cast<unsigned char*>(request.value.virtual_address)[status_offset];
            if (status != status_ok)
                return status == status_unsupp ? -95 : -5;
            if (!write && buffer != nullptr &&
                astia_module_copy_out(buffer, data_length,
                                      static_cast<unsigned char*>(request.value.virtual_address) + 64) != 0)
                return -14;
            return static_cast<long>(data_length);
        }

        long transfer(const void* buffer, unsigned length, uint64_t offset, bool write) {
            if (!valid_transfer(length, offset, write) || (write && !flush_supported))
                return write ? -30 : -22;
            const unsigned data_offset = 64;
            const unsigned status_offset = data_offset + length;
            const unsigned pages = (status_offset + 1 + page_size - 1) / page_size;
            dma request;
            if (!request.allocate(pages))
                return -12;
            if (write && astia_module_copy_in(static_cast<unsigned char*>(request.value.virtual_address) +
                                                   data_offset,
                                               length, buffer) != 0) {
                request.release();
                return -14;
            }
            if (!enqueue(request, data_offset, length, write, status_offset,
                         write ? request_out : request_in, offset)) {
                request.release();
                return -5;
            }
            const long result = wait(request, length, status_offset,
                                     write ? nullptr : const_cast<void*>(buffer), write);
            request.release();
            if (result < 0 || !write)
                return result;
            const long flush_result = flush();
            return flush_result < 0 ? flush_result : static_cast<long>(length);
        }

        long flush() {
            if (!started || !flush_supported)
                return -30;
            dma request;
            if (!request.allocate(1))
                return -12;
            const bool queued = enqueue(request, 0, 0, false, 16, request_flush, 0);
            const long result = queued ? wait(request, 0, 16, nullptr, false) : -5;
            request.release();
            return result < 0 ? result : 0;
        }

        int submit_async(astia_module_io_request* io) {
            if (io == nullptr || pending.active || (io->direction != 0 && io->direction != 1))
                return -16;
            const bool write = io->direction == 1;
            if (write && !write_supported)
                return -30;
            if (!valid_transfer(io->length, io->offset, write))
                return -22;
            const unsigned status_offset = 64 + io->length;
            const unsigned pages = (status_offset + 1 + page_size - 1) / page_size;
            pending = {};
            if (!pending.request.allocate(pages))
                return -12;
            pending.io = io;
            pending.length = io->length;
            pending.status_offset = status_offset;
            pending.write = write;
            pending.active = true;
            if (write && astia_module_copy_in(static_cast<unsigned char*>(pending.request.value.virtual_address) +
                                                   64,
                                               io->length, io->buffer) != 0) {
                pending.request.release();
                pending = {};
                return -14;
            }
            if (!enqueue(pending.request, 64, io->length, write, status_offset,
                         write ? request_out : request_in, io->offset)) {
                pending.request.release();
                pending = {};
                return -5;
            }
            io->poll_timeout_ns = 2000000000ull;
            return 0;
        }

        int poll_async(astia_module_io_request* io) {
            if (!pending.active || pending.io != io)
                return -2;
            if (io->timeout_expired) {
                if (ring_reset)
                    (void) reset_queue_after_timeout();
                pending.request.release();
                pending = {};
                return -110;
            }
            volatile used_ring* used = request_queue.used();
            if (used->index == request_queue.used_index)
                return 0;
            const volatile used_element& element =
                used->ring[request_queue.used_index % request_queue.size];
            ++request_queue.used_index;
            const unsigned expected_length = pending.flush ? 0 : pending.length;
            if (element.id != 0 || element.length < expected_length + 1) {
                pending.request.release();
                pending = {};
                return -5;
            }
            auto* bytes = static_cast<unsigned char*>(pending.request.value.virtual_address);
            const long status_result = bytes[pending.status_offset] == status_ok
                                            ? static_cast<long>(pending.length)
                                            : -5;
            if (status_result < 0) {
                pending.request.release();
                pending = {};
                return -5;
            }
            if (!pending.write) {
                const long result = astia_module_copy_out(pending.io->buffer, pending.length,
                                                           bytes + 64) == 0
                                        ? static_cast<long>(pending.length)
                                        : -14;
                astia_module_io_complete(pending.io->opaque, result);
                pending.request.release();
                pending = {};
                return 0;
            }
            if (!pending.flush) {
                pending.flush = true;
                pending.status_offset = 16;
                if (!enqueue(pending.request, 0, 0, false, 16, request_flush, 0)) {
                    pending.request.release();
                    pending = {};
                    return -5;
                }
                return 0;
            }
            astia_module_io_complete(pending.io->opaque, pending.length);
            pending.request.release();
            pending = {};
            return 0;
        }

        int cancel_async(astia_module_io_request* io) {
            if (!pending.active || pending.io != io)
                return -2;
            pending.canceled = true;
            if (ring_reset)
                (void) reset_queue_after_timeout();
            pending.request.release();
            pending = {};
            return 0;
        }

        long read(void* buffer, unsigned length, uint64_t offset) {
            return transfer(buffer, length, offset, false);
        }

        long write(const void* buffer, unsigned length, uint64_t offset) {
            return transfer(buffer, length, offset, true);
        }
    };

    controller device;

    int match(const astia_module_pci_device* value) {
        return value != nullptr && value->vendor_id == pci_vendor_virtio &&
               value->device_id == pci_device_blk && value->class_code == 1 &&
               value->subclass == 0 && value->programming_interface == 0 && value->bar_count != 0;
    }

    int probe(const astia_module_pci_device* value, void** context) {
        if (value == nullptr || context == nullptr || device.started)
            return 0;
        device.pci = value->opaque;
        if (!device.start()) {
            device.release();
            return 0;
        }
        *context = &device;
        return 1;
    }

    void remove(void* context) {
        if (context == &device)
            device.release();
    }

    uint64_t size(void* context) {
        return static_cast<controller*>(context)->capacity * 512;
    }

    uint32_t block_size(void* context) {
        return static_cast<controller*>(context)->block_size;
    }

    long read(void* context, void* buffer, uint32_t length, uint64_t offset) {
        return static_cast<controller*>(context)->read(buffer, length, offset);
    }

    long write(void* context, const void* buffer, uint32_t length, uint64_t offset) {
        return static_cast<controller*>(context)->write(buffer, length, offset);
    }

    int async_submit(void* context, astia_module_io_request* request) {
        return static_cast<controller*>(context)->submit_async(request);
    }

    int async_cancel(void* context, astia_module_io_request* request) {
        return static_cast<controller*>(context)->cancel_async(request);
    }

    int async_poll(void* context, astia_module_io_request* request) {
        return static_cast<controller*>(context)->poll_async(request);
    }

    int selftest() {
        char sector[512]{};
        const int first = device.read(sector, sizeof sector, 0) == sizeof sector ? 0 : -5;
        if (!device.started || first != 0) {
            return 0;
        }
        char invalid[512]{};
        if (astia_module_read_registered("/dev/vblk0", invalid, sizeof invalid, 1) >= 0 ||
            astia_module_read_registered("/dev/vblk0", invalid, sizeof invalid,
                                         device.capacity * 512) >= 0)
            return 0;
        const bool known = sector[0] == 'A' && sector[1] == 'S' && sector[2] == 'T' &&
                           sector[3] == 'I' && sector[4] == 'A' && sector[5] == '_' &&
                           sector[6] == 'V' && sector[7] == 'I';
        if (!known)
            return 0;
        astia_module_log("vblk", "known sector ok");
        astia_module_log("vblk", "invalid geometry rejected");
        if (!device.write_supported) {
            astia_module_log("vblk", "write rejected: read-only or no flush");
            return 1;
        }
        char written[512]{};
        written[0] = 'A';
        written[1] = 'S';
        written[2] = 'T';
        written[3] = 'I';
        written[4] = 'A';
        written[5] = '_';
        written[6] = 'W';
        written[7] = 'R';
        if (device.write(written, sizeof written, 512) != sizeof written)
            return 0;
        char persisted[512]{};
        if (device.read(persisted, sizeof persisted, 512) != sizeof persisted ||
            persisted[0] != 'A' || persisted[6] != 'W' || persisted[7] != 'R')
            return 0;
        astia_module_log("vblk", "write flush persistence ok");
        return 1;
    }

    const astia_module_pci_block_driver ops = {
        "virtio-blk", "/dev/vblk0", match, probe, remove, size, block_size, read, selftest,
        async_submit, async_cancel, async_poll, write};

}

extern "C" int module_main() {
    return astia_module_register_pci_block_driver(&ops);
}
