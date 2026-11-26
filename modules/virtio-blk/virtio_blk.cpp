#include <astia/atomic.hpp>
#include <astia/core/endian.hpp>
#include <astia/dev/blk/blkdev.hpp>
#include <astia/dev/device_file.hpp>
#include <astia/dev/driver.hpp>
#include <astia/dev/regs.hpp>
#include <astia/fs.hpp>
#include <astia/hw/pci.hpp>
#include <astia/io.hpp>
#include <astia/mm/dma_buffer.hpp>
#include <astia/mm/share.hpp>
#include <astia/module.hpp>

#include "virtio_blk_protocol.hpp"

namespace {

    void copy_bytes(void* destination, const void* source, unsigned length) {
        auto*       to   = static_cast<unsigned char*>(destination);
        const auto* from = static_cast<const unsigned char*>(source);
        for (unsigned i = 0; i < length; ++i)
            to[i] = from[i];
    }

    constexpr unsigned queue_limit        = 8;
    constexpr unsigned timeout_spins      = 4000000;
    constexpr unsigned pci_vendor_virtio  = 0x1af4;
    constexpr unsigned pci_device_blk     = 0x1042;
    constexpr unsigned virtio_capability  = 0x09;
    constexpr unsigned cap_common         = 1;
    constexpr unsigned cap_notify         = 2;
    constexpr unsigned cap_device         = 4;
    constexpr unsigned feature_version_1  = 32;
    constexpr unsigned feature_ring_reset = 40;
    constexpr unsigned feature_blk_ro     = 5;
    constexpr unsigned feature_blk_flush  = 9;
    constexpr unsigned _requestin         = 0;
    constexpr unsigned _requestout        = 1;
    constexpr unsigned _requestflush      = 4;
    constexpr unsigned status_ok          = 0;
    constexpr unsigned status_unsupp      = 2;
    constexpr unsigned status_acknowledge = 1;
    constexpr unsigned status_driver      = 2;
    constexpr unsigned status_features_ok = 8;
    constexpr unsigned status_driver_ok   = 4;
    constexpr unsigned descriptor_next    = 1;
    constexpr unsigned descriptor_write   = 2;

    using dma = astia::mm::dma_buffer;

    struct capability_region {
        bool          present = false;
        unsigned char bar     = 0;
        uint64_t      offset  = 0;
        uint64_t      length  = 0;
    };

    struct descriptor {
        astia::core::le<uint64_t> address;
        astia::core::le<uint32_t> length;
        astia::core::le<uint16_t> flags;
        astia::core::le<uint16_t> next;

        descriptor(uint64_t _addressvalue, uint32_t length_value, uint16_t flags_value,
                   uint16_t _nextvalue)
            : address(_addressvalue), length(length_value), flags(flags_value),
              next(_nextvalue) {}
    };

    struct available_ring {
        astia::core::le<uint16_t> flags;
        astia::core::le<uint16_t> index;
        astia::core::le<uint16_t> ring[queue_limit];
    };

    struct used_element {
        astia::core::le<uint32_t> id;
        astia::core::le<uint32_t> length;
    };

    struct used_ring {
        astia::core::le<uint16_t> flags;
        astia::core::le<uint16_t> index;
        used_element              ring[queue_limit];
    };

    class mmio {
        public:
        constexpr mmio() = default;
        explicit mmio(volatile void* base) : _bytes(base), _halves(base), _words(base) {}

        template <typename T>
        T read(unsigned offset) const {
            if constexpr (sizeof(T) == 1)
                return _bytes[offset];
            else if constexpr (sizeof(T) == 2)
                return astia::core::le<T>(_halves[offset]).get();
            else if constexpr (sizeof(T) == 4)
                return astia::core::le<T>(_words[offset]).get();
            else
                return static_cast<T>(read<uint32_t>(offset)) |
                       (static_cast<T>(read<uint32_t>(offset + 4)) << 32);
        }

        template <typename T>
        void write(unsigned offset, T value) const {
            if constexpr (sizeof(T) == 1)
                _bytes[offset] = value;
            else if constexpr (sizeof(T) == 2)
                _halves[offset] = astia::core::le<T>(value).get();
            else if constexpr (sizeof(T) == 4)
                _words[offset] = astia::core::le<T>(value).get();
            else {
                write<uint32_t>(offset, static_cast<uint32_t>(value));
                write<uint32_t>(offset + 4, static_cast<uint32_t>(value >> 32));
            }
        }

        bool mapped() const {
            return _bytes.base() != nullptr;
        }

        private:
        astia::dev::regs<unsigned char> _bytes;
        astia::dev::regs<uint16_t>      _halves;
        astia::dev::regs<uint32_t>      _words;
    };

    struct queue {
        dma      memory;
        unsigned size              = 0;
        unsigned descriptor_offset = 0;
        unsigned available_offset  = 0;
        unsigned used_offset       = 0;
        uint16_t available_index   = 0;
        uint16_t used_index        = 0;
        uint16_t notify_offset     = 0;
        bool     enabled           = false;

        descriptor* descriptors() const {
            return reinterpret_cast<descriptor*>(
                static_cast<unsigned char*>(memory.data()) + descriptor_offset);
        }

        available_ring* available() const {
            return reinterpret_cast<available_ring*>(
                static_cast<unsigned char*>(memory.data()) + available_offset);
        }

        volatile used_ring* used() const {
            return static_cast<volatile used_ring*>(static_cast<void*>(
                static_cast<unsigned char*>(memory.data()) + used_offset));
        }

        void clear() {
            auto* bytes = static_cast<unsigned char*>(memory.data());
            for (unsigned i = 0; i < astia::mm::page_size; ++i)
                bytes[i] = 0;
            available_index = 0;
            used_index      = 0;
        }
    };

    void memory_barrier() {
        asm volatile("mfence" ::: "memory");
    }

    uint8_t pci_byte(const astia::hw::pci_device* pci, uint16_t offset) {
        const uint32_t value =
            astia::module::pci_read32(pci, static_cast<uint16_t>(offset & ~3u));
        return static_cast<uint8_t>(value >> ((offset & 3u) * 8u));
    }

    uint32_t pci_dword(const astia::hw::pci_device* pci, uint16_t offset) {
        return astia::module::pci_read32(pci, static_cast<uint16_t>(offset & ~3u));
    }

    struct controller {
        astia::hw::pci_device* pci = nullptr;
        mmio                   common{};
        mmio                   notify{};
        mmio                   device_config{};
        capability_region      common_cap{};
        capability_region      notify_cap{};
        capability_region      device_cap{};
        uint32_t               device_features_low  = 0;
        uint32_t               device_features_high = 0;
        uint64_t               capacity             = 0;
        uint32_t               block_size           = 512;
        uint32_t               notify_multiplier    = 0;
        bool                   ring_reset           = false;
        bool                   flush_supported      = false;
        bool                   write_supported      = false;
        bool                   started              = false;
        bool                   failed               = false;
        bool                   quiescing            = false;
        bool                   quiesce_safe         = true;
        bool                   release_pending      = false;
        queue                  _requestqueue{};

        bool region_valid(const capability_region& region, unsigned required) const {
            if (!region.present || region.bar >= 6 ||
                region.offset > ~uint64_t{0} - required ||
                region.offset + required > astia::module::pci_bar_size(pci, region.bar))
                return false;
            return true;
        }

        bool map_region(const capability_region& region, mmio& result) {
            if (!region.present || region.bar >= 6 ||
                region.offset > ~uint64_t{0} - region.length ||
                region.offset + region.length >
                    astia::module::pci_bar_size(pci, region.bar))
                return false;
            uint64_t mapped = astia::module::pci_bar_mapped(pci, region.bar);
            if (mapped == 0) {
                if (!astia::module::pci_map_bar(pci, region.bar))
                    return false;
                mapped = astia::module::pci_bar_mapped(pci, region.bar);
            }
            if (mapped == 0)
                return false;
            result = mmio{reinterpret_cast<void*>(mapped + region.offset)};
            return true;
        }

        bool find_capabilities() {
            common_cap            = {};
            notify_cap            = {};
            device_cap            = {};
            const uint8_t pointer = pci_byte(pci, 0x34);
            if (pointer == 0 || pointer == 0xff)
                return false;
            bool    visited[256]{};
            uint8_t current = pointer;
            for (unsigned step = 0; step < 48 && current != 0; ++step) {
                if (current < 0x40 || current > 0xfc || (current & 3u) != 0 ||
                    visited[current])
                    return false;
                visited[current]     = true;
                const uint8_t id     = pci_byte(pci, current);
                const uint8_t next   = pci_byte(pci, static_cast<uint16_t>(current + 1));
                const uint8_t length = pci_byte(pci, static_cast<uint16_t>(current + 2));
                if (id == virtio_capability && length >= 16) {
                    const uint8_t type =
                        pci_byte(pci, static_cast<uint16_t>(current + 3));
                    const uint8_t bar = pci_byte(pci, static_cast<uint16_t>(current + 4));
                    const uint64_t offset =
                        pci_dword(pci, static_cast<uint16_t>(current + 8));
                    const uint64_t region_length =
                        pci_dword(pci, static_cast<uint16_t>(current + 12));
                    capability_region* destination = type == cap_common   ? &common_cap
                                                     : type == cap_notify ? &notify_cap
                                                     : type == cap_device ? &device_cap
                                                                          : nullptr;
                    if (destination != nullptr && !destination->present) {
                        destination->present = true;
                        destination->bar     = bar;
                        destination->offset  = offset;
                        destination->length  = region_length;
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
                if (common.read<uint8_t>(0x14) == expected)
                    return true;
                asm volatile("pause");
            }
            return false;
        }

        void set_status(uint8_t value) {
            common.write<uint8_t>(0x14, value);
        }

        bool reset_device() {
            set_status(0);
            return wait_status(0);
        }

        bool negotiate_features() {
            common.write<uint32_t>(0x00, 0);
            device_features_low = common.read<uint32_t>(0x04);
            common.write<uint32_t>(0x00, 1);
            device_features_high = common.read<uint32_t>(0x04);
            if ((device_features_high & (1u << (feature_version_1 - 32))) == 0)
                return false;
            const uint32_t accepted_low =
                device_features_low &
                ((1u << feature_blk_ro) | (1u << feature_blk_flush));
            const uint32_t accepted_high =
                device_features_high & (1u << (feature_version_1 - 32));
            ring_reset =
                (device_features_high & (1u << (feature_ring_reset - 32))) != 0 &&
                common_cap.length >= 0x3a;
            flush_supported = (device_features_low & (1u << feature_blk_flush)) != 0;
            write_supported =
                flush_supported && (device_features_low & (1u << feature_blk_ro)) == 0;
            common.write<uint32_t>(0x08, 0);
            common.write<uint32_t>(0x0c, accepted_low);
            common.write<uint32_t>(0x08, 1);
            common.write<uint32_t>(0x0c, accepted_high);
            set_status(status_acknowledge | status_driver | status_features_ok);
            const uint8_t status = common.read<uint8_t>(0x14);
            return (status & status_features_ok) != 0;
        }

        bool read_configuration() {
            if (!region_valid(device_cap, 8))
                return false;
            for (unsigned attempt = 0; attempt < 16; ++attempt) {
                const uint8_t  before  = common.read<uint8_t>(0x15);
                const uint64_t sectors = device_config.read<uint64_t>(0);
                const uint8_t  after   = common.read<uint8_t>(0x15);
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
            common.write<uint16_t>(0x16, 0);
            const unsigned maximum = common.read<uint16_t>(0x18);
            if (maximum == 0)
                return false;
            unsigned size = maximum < queue_limit ? maximum : queue_limit;
            while (size != 0 && (size & (size - 1u)) != 0)
                size &= size - 1u;
            if (size < 4)
                return false;
            _requestqueue.size              = size;
            _requestqueue.descriptor_offset = 0;
            _requestqueue.available_offset  = size * 16;
            _requestqueue.used_offset =
                (_requestqueue.available_offset + 6 + size * 2 + 3) & ~3u;
            if (_requestqueue.used_offset + 6 + size * 8 > astia::mm::page_size)
                return false;
            if (!_requestqueue.memory.allocate(1))
                return false;
            _requestqueue.clear();
            common.write<uint16_t>(0x18, static_cast<uint16_t>(size));
            common.write<uint16_t>(0x1a, 0xffff);
            common.write<uint64_t>(0x20, _requestqueue.memory.physical() +
                                             _requestqueue.descriptor_offset);
            common.write<uint64_t>(0x28, _requestqueue.memory.physical() +
                                             _requestqueue.available_offset);
            common.write<uint64_t>(0x30, _requestqueue.memory.physical() +
                                             _requestqueue.used_offset);
            _requestqueue.notify_offset = common.read<uint16_t>(0x1e);
            const uint64_t notify_offset =
                static_cast<uint64_t>(_requestqueue.notify_offset) * notify_multiplier;
            if (notify_offset > notify_cap.length ||
                notify_offset + 2 > notify_cap.length) {
                _requestqueue.memory.release();
                return false;
            }
            common.write<uint16_t>(0x1c, 1);
            if (common.read<uint16_t>(0x1c) != 1) {
                _requestqueue.memory.release();
                return false;
            }
            _requestqueue.enabled = true;
            return true;
        }

        bool disable_queue() {
            if (!_requestqueue.enabled)
                return true;
            bool reset = true;
            if (ring_reset) {
                common.write<uint16_t>(0x38, 1);
                reset = false;
                for (unsigned spin = 0; spin < timeout_spins; ++spin) {
                    if (common.read<uint16_t>(0x38) == 0) {
                        reset = true;
                        break;
                    }
                    asm volatile("pause");
                }
            }
            _requestqueue.enabled = false;
            return reset;
        }

        void release() {
            quiesce();
            if (!quiesce_safe) {
                release_pending = true;
                return;
            }
            if (pending.active) {
                release_pending = true;
                return;
            }
            release_pending = false;
            _requestqueue.memory.release();
            common        = {};
            notify        = {};
            device_config = {};
            started       = false;
        }

        void quiesce() {
            if (quiescing)
                return;
            quiescing    = true;
            quiesce_safe = disable_queue();
            if (common.mapped())
                quiesce_safe = reset_device() && quiesce_safe;
            if (!pending.active)
                return;
            pending.completed     = true;
            pending.result        = -19;
            pending.hardware_done = quiesce_safe;
            if (pending.io != nullptr && quiesce_safe) {
                if (pending.io->device_state() != nullptr)
                    pending.io->wake();
            }
            else if (pending.io == nullptr && quiesce_safe) {
                pending.request.release();
                pending = {};
            }
        }

        void stop() {
            quiesce();
            release();
        }

        bool start() {
            quiescing = false;
            if (!astia::module::pci_enable_bus_master(pci) ||
                !astia::module::pci_disable_msix(pci) || !find_capabilities())
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
            set_status(status_acknowledge | status_driver | status_features_ok |
                       status_driver_ok);
            if ((common.read<uint8_t>(0x14) & status_driver_ok) == 0)
                return false;
            started = true;
            astia::module::log_u64("vblk", "sectors", capacity);
            return true;
        }

        bool reset_queue_after_timeout() {
            if (!ring_reset)
                return false;
            if (!disable_queue())
                return false;
            _requestqueue.clear();
            common.write<uint16_t>(0x18, static_cast<uint16_t>(_requestqueue.size));
            common.write<uint64_t>(0x20, _requestqueue.memory.physical() +
                                             _requestqueue.descriptor_offset);
            common.write<uint64_t>(0x28, _requestqueue.memory.physical() +
                                             _requestqueue.available_offset);
            common.write<uint64_t>(0x30, _requestqueue.memory.physical() +
                                             _requestqueue.used_offset);
            common.write<uint16_t>(0x1c, 1);
            if (common.read<uint16_t>(0x1c) != 1)
                return false;
            _requestqueue.enabled = true;
            return true;
        }

        struct pending_request {
            dma                 request{};
            astia::io::request* io            = nullptr;
            long                result        = 0;
            bool                completed     = false;
            unsigned            length        = 0;
            unsigned            data_offset   = 64;
            unsigned            status_offset = 0;
            bool                write         = false;
            bool                flush         = false;
            bool                canceled      = false;
            bool                active        = false;
            bool                hardware_done = false;
        } pending{};

        bool valid_transfer(unsigned length, uint64_t offset, bool write) const {
            if (!started || failed || length > astia_virtio_blk::max_transfer ||
                !astia_virtio_blk::valid_range(capacity, length, offset))
                return false;
            return !write || write_supported;
        }

        bool enqueue(dma& request, unsigned data_offset, unsigned data_length, bool write,
                     unsigned status_offset, unsigned type, uint64_t offset) {
            auto* bytes  = static_cast<unsigned char*>(request.data());
            auto* header = reinterpret_cast<uint32_t*>(bytes);
            header[0]    = type;
            header[1]    = 0;
            reinterpret_cast<uint64_t*>(bytes + 8)[0] =
                type == _requestflush ? 0 : offset / 512;
            bytes[status_offset]    = 0xff;
            descriptor* descriptors = _requestqueue.descriptors();
            descriptors[0] = descriptor{request.physical(), 16, descriptor_next, 1};
            if (data_length != 0) {
                descriptors[1] =
                    descriptor{request.physical() + data_offset, data_length,
                               static_cast<uint16_t>(descriptor_next |
                                                     (write ? 0 : descriptor_write)),
                               2};
                descriptors[2] = descriptor{request.physical() + status_offset, 1,
                                            descriptor_write, 0};
            }
            else {
                descriptors[1] = descriptor{request.physical() + status_offset, 1,
                                            descriptor_write, 0};
            }
            available_ring* available = _requestqueue.available();
            available->ring[_requestqueue.available_index % _requestqueue.size] = 0;
            memory_barrier();
            ++_requestqueue.available_index;
            available->index = _requestqueue.available_index;
            memory_barrier();
            const uint64_t notify_offset =
                static_cast<uint64_t>(_requestqueue.notify_offset) * notify_multiplier;
            if (notify_offset + 2 > notify_cap.length)
                return false;
            notify.write<uint16_t>(notify_offset, 0);
            return true;
        }

        long wait(dma& request, unsigned data_length, unsigned status_offset,
                  void* buffer, bool write) {
            volatile used_ring* used      = _requestqueue.used();
            bool                completed = false;
            bool                observed  = false;
            for (unsigned spin = 0; spin < timeout_spins; ++spin) {
                memory_barrier();
                if (used->index != _requestqueue.used_index) {
                    const volatile used_element& element =
                        used->ring[_requestqueue.used_index % _requestqueue.size];
                    ++_requestqueue.used_index;
                    observed  = true;
                    completed = element.id == 0 && astia_virtio_blk::completion_length_ok(
                                                       write ? _requestout : _requestin,
                                                       data_length, element.length);
                    break;
                }
                asm volatile("pause");
            }
            if (!completed) {
                if (!observed && !reset_queue_after_timeout())
                    failed = true;
                return observed ? -5 : -110;
            }
            const uint8_t status =
                static_cast<unsigned char*>(request.data())[status_offset];
            if (status != status_ok)
                return status == status_unsupp ? -95 : -5;
            if (!write && buffer != nullptr)
                copy_bytes(buffer, static_cast<unsigned char*>(request.data()) + 64,
                           data_length);
            return static_cast<long>(data_length);
        }

        long transfer(const void* buffer, unsigned length, uint64_t offset, bool write) {
            if (!valid_transfer(length, offset, write) || (write && !flush_supported))
                return write ? -30 : -22;
            const unsigned data_offset   = 64;
            const unsigned status_offset = data_offset + length;
            const unsigned pages = astia_virtio_blk::page_count(status_offset + 1);
            dma            request;
            if (!request.allocate(pages))
                return -12;
            if (write)
                copy_bytes(static_cast<unsigned char*>(request.data()) + data_offset,
                           buffer, length);
            if (!enqueue(request, data_offset, length, write, status_offset,
                         write ? _requestout : _requestin, offset)) {
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
            const bool queued = enqueue(request, 0, 0, false, 16, _requestflush, 0);
            const long result = queued ? wait(request, 0, 16, nullptr, false) : -5;
            request.release();
            return result < 0 ? result : 0;
        }

        int submit_async(astia::io::request& io) {
            if (quiescing || pending.active ||
                (io.direction() != astia::io::direction::input &&
                 io.direction() != astia::io::direction::output))
                return -16;
            const bool write = io.direction() == astia::io::direction::output;
            if (write && !write_supported)
                return -30;
            if (!valid_transfer(io.length(), io.offset(), write))
                return -22;
            const unsigned status_offset = 64 + io.length();
            const unsigned pages = astia_virtio_blk::page_count(status_offset + 1);
            pending              = {};
            if (!pending.request.allocate(pages))
                return -12;
            pending.io            = &io;
            pending.length        = io.length();
            pending.status_offset = status_offset;
            pending.write         = write;
            pending.active        = true;
            pending.hardware_done = false;
            if (write)
                copy_bytes(static_cast<unsigned char*>(pending.request.data()) + 64,
                           io.buffer(), io.length());
            if (!enqueue(pending.request, 64, io.length(), write, status_offset,
                         write ? _requestout : _requestin, io.offset())) {
                pending.request.release();
                pending = {};
                return -5;
            }
            io.timeout_ns(2000000000ull);
            io.poll_interval_ns(1000000);
            return 0;
        }

        int poll_async(astia::io::request& io) {
            if (!pending.active || pending.io != &io)
                return -2;
            if (pending.completed) {
                if (!pending.hardware_done) {
                    volatile used_ring* used = _requestqueue.used();
                    if (used->index != _requestqueue.used_index) {
                        ++_requestqueue.used_index;
                        pending.hardware_done = true;
                        if (pending.io != nullptr &&
                            pending.io->device_state() != nullptr)
                            pending.io->wake();
                    }
                }
                if (pending.io == nullptr && pending.hardware_done) {
                    pending.request.release();
                    pending = {};
                }
                return 0;
            }
            if (io.timeout_expired()) {
                pending.hardware_done = ring_reset && reset_queue_after_timeout();
                pending.result        = -110;
                pending.completed     = true;
                if (pending.hardware_done && pending.io->device_state() != nullptr)
                    pending.io->wake();
                return 0;
            }
            volatile used_ring* used = _requestqueue.used();
            if (used->index == _requestqueue.used_index)
                return 0;
            const volatile used_element& element =
                used->ring[_requestqueue.used_index % _requestqueue.size];
            ++_requestqueue.used_index;
            const unsigned expected_length = pending.flush ? 0 : pending.length;
            if (element.id != 0 || element.length < expected_length + 1) {
                pending.request.release();
                pending = {};
                return -5;
            }
            auto*      bytes = static_cast<unsigned char*>(pending.request.data());
            const long status_result = bytes[pending.status_offset] == status_ok
                                           ? static_cast<long>(pending.length)
                                           : -5;
            if (status_result < 0) {
                pending.request.release();
                pending = {};
                return -5;
            }
            if (!pending.write) {
                copy_bytes(pending.io->buffer(), bytes + 64, pending.length);
                pending.result        = static_cast<long>(pending.length);
                pending.completed     = true;
                pending.hardware_done = true;
                if (pending.io->device_state() != nullptr)
                    pending.io->wake();
                return 0;
            }
            if (!pending.flush) {
                pending.flush         = true;
                pending.status_offset = 16;
                if (!enqueue(pending.request, 0, 0, false, 16, _requestflush, 0)) {
                    pending.request.release();
                    pending = {};
                    return -5;
                }
                return 0;
            }
            pending.result        = pending.length;
            pending.completed     = true;
            pending.hardware_done = true;
            if (pending.io->device_state() != nullptr)
                pending.io->wake();
            return 0;
        }

        int cancel_async(astia::io::request& io, bool retain) {
            if (!pending.active || pending.io != &io)
                return -2;
            pending.canceled  = true;
            pending.completed = true;
            pending.result    = -125;
            if (!retain)
                pending.io = nullptr;
            pending.hardware_done =
                quiescing || (ring_reset && reset_queue_after_timeout());
            if (retain && pending.hardware_done && pending.io->device_state() != nullptr)
                pending.io->wake();
            return 0;
        }

        bool complete_async(astia::io::request& io, long& result) {
            if (!pending.active || pending.io != &io || !pending.completed ||
                !pending.hardware_done)
                return false;
            result = pending.result;
            pending.request.release();
            pending = {};
            if (release_pending)
                release();
            return true;
        }

        long read(void* buffer, unsigned length, uint64_t offset) {
            return transfer(buffer, length, offset, false);
        }

        long write(const void* buffer, unsigned length, uint64_t offset) {
            return transfer(buffer, length, offset, true);
        }
    };

#ifndef ASTIA_TEARDOWN_TEST
    controller device;

    class virtio_block final : public astia::dev::blk::blkdev {
        public:
        explicit virtio_block(controller& owner)
            : blkdev("vblk", owner.capacity * 512, owner.block_size), _owner(owner) {}

        long submit(astia::io::request& request) override {
            const long valid = validate_request(request);
            if (valid != 0)
                return valid;
            if (request.device_state() == this) {
                _owner.poll_async(request);
                long result = 0;
                if (!_owner.complete_async(request, result))
                    return 0;
                request.device_state(nullptr);
                return result;
            }
            if (request.device_state() != nullptr)
                return -16;
            request.device_state(this);
            const int result = _owner.submit_async(request);
            if (result < 0) {
                request.device_state(nullptr);
                return result;
            }
            return 0;
        }

        protected:
        long read_blocks(void* buffer, astia::u32 length, astia::u64 offset) override {
            return _owner.read(buffer, length, offset);
        }
        long write_blocks(const void* buffer, astia::u32 length,
                          astia::u64 offset) override {
            return _owner.write(buffer, length, offset);
        }

        private:
        controller& _owner;
    };

    class virtio_driver final : public astia::hw::driver {
        public:
        const char* name() const override {
            return "virtio-blk";
        }
        bool match_pci(const astia::hw::pci_device& value) const {
            return value.vendor_id == pci_vendor_virtio &&
                   value.device_id == pci_device_blk && value.class_code == 1 &&
                   value.subclass == 0 && value.programming_interface == 0 &&
                   value.bar_count != 0;
        }
        bool probe_pci(astia::hw::pci_device& value) {
            if (device.started)
                return false;
            device.pci = &value;
            return device.start() || (device.release(), false);
        }
        bool match(const astia::hw::device_info& info) const override {
            const auto* pci = astia::hw::pci_device_info::get(info);
            return pci != nullptr && match_pci(pci->device());
        }
        bool probe(const astia::hw::device_info& info) override {
            astia::u64 token = astia::atomic_ref<astia::u64>(_teardown_token)
                                   .load(astia::memory_order::acquire);
            const astia::u64 state = teardown_state(token);
            if (state == teardown_complete &&
                !astia::atomic_ref<astia::u64>(_teardown_token)
                     .compare_exchange_strong(
                         token, teardown_word(teardown_generation(token), 0),
                         astia::memory_order::acq_rel, astia::memory_order::acquire))
                return {};
            if (state != 0 && state != teardown_complete)
                return {};
            const auto* pci = astia::hw::pci_device_info::get(info);
            if (pci == nullptr)
                return {};
            auto* stable = astia::hw::pci_devices().find(pci->device().bdf);
            if (stable == nullptr || !probe_pci(*stable))
                return {};
            auto block = astia::mm::share<virtio_block>(device);
            if (!block) {
                device.release();
                return {};
            }
            astia::mm::shptr<astia::dev::device> owner = block;
            auto file = astia::mm::share<astia::dev::device_file>(owner);
            if (!file) {
                device.release();
                return {};
            }
            if (astia::fs::register_device("/dev/vblk0", file) < 0) {
                device.release();
                return {};
            }
            _file    = file;
            _product = block;
            return true;
        }
        astia::hw::product_handle publish(const astia::hw::device_info&) override {
            return _product;
        }
        void remove(const astia::hw::device_info&) override {
            astia::u64 expected = astia::atomic_ref<astia::u64>(_teardown_token)
                                      .load(astia::memory_order::acquire);
            const astia::u64 generation =
                teardown_generation(expected) == teardown_generation_mask
                    ? 1
                    : teardown_generation(expected) + 1;
            const astia::u64 state = teardown_state(expected);
            if (state != 0 && state != teardown_complete)
                return;
            if (!astia::atomic_ref<astia::u64>(_teardown_token)
                     .compare_exchange_strong(
                         expected, teardown_word(generation, teardown_claiming),
                         astia::memory_order::acq_rel, astia::memory_order::acquire))
                return;
            if (_file) {
                _file->detach();
                (void) astia::fs::unregister_device("/dev/vblk0", _file);
            }
            if (_file)
                (void) _file->arm_quiescence_handler(quiescence_callback, this,
                                                     generation);
            astia::atomic_ref<astia::u64>(_teardown_token)
                .store(teardown_word(generation, teardown_waiting),
                       astia::memory_order::release);
            finish_remove(generation);
        }
        bool selftest() override {
            char      sector[512]{};
            const int first =
                device.read(sector, sizeof sector, 0) == sizeof sector ? 0 : -5;
            if (!device.started || first != 0)
                return false;
            char invalid[512]{};
            if (device.read(invalid, sizeof invalid, 1) >= 0 ||
                device.read(invalid, sizeof invalid, device.capacity * 512) >= 0)
                return false;
            const bool known = sector[0] == 'A' && sector[1] == 'S' && sector[2] == 'T' &&
                               sector[3] == 'I' && sector[4] == 'A' && sector[5] == '_' &&
                               sector[6] == 'V' && sector[7] == 'I';
            if (!known)
                return false;
            astia::module::log("vblk", "known sector ok");
            astia::module::log("vblk", "invalid geometry rejected");
            char async_read[512]{};
            if (astia::module::read_registered_async("/dev/vblk0", async_read,
                                                     sizeof async_read, 0) != 0 ||
                async_read[0] != 'A' || async_read[1] != 'S' || async_read[2] != 'T' ||
                async_read[3] != 'I' || async_read[4] != 'A' || async_read[5] != '_' ||
                async_read[6] != 'V' || async_read[7] != 'I')
                return false;
            if (!device.write_supported) {
                astia::module::log("vblk", "attached kernel-buffer async read ok");
                astia::module::log("vblk", "write rejected: read-only or no flush");
                return true;
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
                return false;
            char persisted[512]{};
            if (device.read(persisted, sizeof persisted, 512) != sizeof persisted ||
                persisted[0] != 'A' || persisted[6] != 'W' || persisted[7] != 'R')
                return false;
            astia::module::log("vblk", "write flush persistence ok");
            return true;
        }

        private:
        static constexpr astia::u64 teardown_claiming        = 1;
        static constexpr astia::u64 teardown_waiting         = 2;
        static constexpr astia::u64 teardown_complete        = 4;
        static constexpr astia::u64 teardown_retry           = 8;
        static constexpr astia::u64 teardown_generation_mask = UINT64_MAX >> 4;

        static astia::u64 teardown_word(astia::u64 generation, astia::u64 state,
                                        bool retry = false) {
            return (generation << 4) | state | (retry ? teardown_retry : 0);
        }

        static astia::u64 teardown_generation(astia::u64 token) {
            return token >> 4;
        }

        static astia::u64 teardown_state(astia::u64 token) {
            return token & 7;
        }

        static void quiescence_callback(void* context, astia::u64 generation) {
            static_cast<virtio_driver*>(context)->finish_remove(generation);
        }

        void finish_remove(astia::u64 generation) {
            astia::u64 expected = astia::atomic_ref<astia::u64>(_teardown_token)
                                      .load(astia::memory_order::acquire);
            for (;;) {
                if (teardown_generation(expected) != generation)
                    return;
                if (teardown_state(expected) == 3) {
                    if ((expected & teardown_retry) != 0)
                        return;
                    const astia::u64 desired = expected | teardown_retry;
                    if (astia::atomic_ref<astia::u64>(_teardown_token)
                            .compare_exchange_strong(expected, desired,
                                                     astia::memory_order::acq_rel,
                                                     astia::memory_order::acquire))
                        return;
                    continue;
                }
                if (teardown_state(expected) != teardown_waiting)
                    return;
                if (astia::atomic_ref<astia::u64>(_teardown_token)
                        .compare_exchange_strong(expected, teardown_word(generation, 3),
                                                 astia::memory_order::acq_rel,
                                                 astia::memory_order::acquire))
                    break;
            }
            if (_file && !_file->quiescent()) {
                astia::u64 observed = astia::atomic_ref<astia::u64>(_teardown_token)
                                          .load(astia::memory_order::acquire);
                for (;;) {
                    if (teardown_generation(observed) != generation ||
                        teardown_state(observed) != 3)
                        return;
                    const bool retry = (observed & teardown_retry) != 0;
                    if (astia::atomic_ref<astia::u64>(_teardown_token)
                            .compare_exchange_strong(
                                observed,
                                teardown_word(generation, teardown_waiting, retry),
                                astia::memory_order::acq_rel,
                                astia::memory_order::acquire)) {
                        if (retry)
                            finish_remove(generation);
                        break;
                    }
                }
                return;
            }
            device.stop();
            _file    = {};
            _product = {};
            astia::atomic_ref<astia::u64>(_teardown_token)
                .store(teardown_word(generation, teardown_complete),
                       astia::memory_order::release);
        }

        controller                                device;
        astia::mm::shptr<astia::dev::device>      _product;
        astia::mm::shptr<astia::dev::device_file> _file;
        astia::u64                                _teardown_token = 0;
    };

    virtio_driver driver;
}

int module_main() {
    return astia::hw::drivers().register_driver(driver) ? 0 : -22;
}
#endif
