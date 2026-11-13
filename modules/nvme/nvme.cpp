#include <astia/module_abi.h>

namespace {

    constexpr unsigned page_size = 4096;
    constexpr unsigned queue_depth = 16;
    constexpr unsigned timeout_spins = 4000000;
    constexpr unsigned admin_identify = 0x06;
    constexpr unsigned admin_create_io_cq = 0x05;
    constexpr unsigned admin_create_io_sq = 0x01;
    constexpr unsigned io_read = 0x02;

    struct dma {
        astia_module_dma_buffer value{};

        bool allocate(bool zero = true) {
            return astia_module_dma_alloc(1, zero ? 1 : 0, &value) == 0;
        }

        void release() {
            astia_module_dma_release(&value);
        }
    };

    struct command {
        unsigned dword[16]{};
    };

    struct completion {
        unsigned dword[4]{};
    };

    struct controller {
        void* pci = nullptr;
        volatile unsigned char* registers = nullptr;
        unsigned long long capability = 0;
        unsigned doorbell_stride = 4;
        unsigned namespace_id = 1;
        unsigned long long namespace_size = 0;
        unsigned block_size = 512;
        unsigned short admin_sq_tail = 0;
        unsigned short admin_cq_head = 0;
        unsigned short io_sq_tail = 0;
        unsigned short io_cq_head = 0;
        unsigned char admin_cq_phase = 1;
        unsigned char io_cq_phase = 1;
        unsigned short admin_cid = 1;
        unsigned short io_cid = 1;
        bool started = false;
        dma admin_sq;
        dma admin_cq;
        dma io_sq;
        dma io_cq;
        dma identify_page;

        unsigned read32(unsigned offset) const {
            return *reinterpret_cast<volatile const unsigned*>(registers + offset);
        }

        unsigned long long read64(unsigned offset) const {
            return static_cast<unsigned long long>(read32(offset)) |
                   (static_cast<unsigned long long>(read32(offset + 4)) << 32);
        }

        void write32(unsigned offset, unsigned value) {
            *reinterpret_cast<volatile unsigned*>(registers + offset) = value;
        }

        void doorbell(unsigned queue, bool completion_queue, unsigned short value) {
            const unsigned index = queue * 2 + (completion_queue ? 1 : 0);
            write32(0x1000 + index * doorbell_stride, value);
        }

        bool wait_ready(bool ready) const {
            for (unsigned spin = 0; spin < timeout_spins; ++spin) {
                const bool observed = (read32(0x1c) & 1) != 0;
                if (observed == ready)
                    return (read32(0x1c) & 2) == 0;
                asm volatile("pause");
            }
            return false;
        }

        void set_address(unsigned offset, unsigned long long address) {
            write32(offset, static_cast<unsigned>(address));
            write32(offset + 4, static_cast<unsigned>(address >> 32));
        }

        bool allocate_queues() {
            return admin_sq.allocate() && admin_cq.allocate() && io_sq.allocate() &&
                   io_cq.allocate() && identify_page.allocate();
        }

        bool reset_and_enable() {
            doorbell_stride = 4u << static_cast<unsigned>((capability >> 32) & 0xf);
            write32(0x14, 0);
            if (!wait_ready(false))
                return false;
            write32(0x24, (queue_depth - 1) << 16 | (queue_depth - 1));
            set_address(0x28, admin_sq.value.physical);
            set_address(0x30, admin_cq.value.physical);
            write32(0x14, (4u << 20) | (6u << 16) | 1u);
            return wait_ready(true);
        }

        void submit_admin(command& value, unsigned long long data) {
            value.dword[6] = static_cast<unsigned>(data);
            value.dword[7] = static_cast<unsigned>(data >> 32);
            auto* entries = static_cast<command*>(admin_sq.value.virtual_address);
            entries[admin_sq_tail] = value;
            admin_sq_tail = static_cast<unsigned short>((admin_sq_tail + 1) % queue_depth);
            doorbell(0, false, admin_sq_tail);
        }

        unsigned poll_admin() {
            auto* entries = static_cast<volatile completion*>(admin_cq.value.virtual_address);
            for (unsigned spin = 0; spin < timeout_spins; ++spin) {
                const unsigned dw3 = entries[admin_cq_head].dword[3];
                if (((dw3 >> 16) & 1) == admin_cq_phase) {
                    const unsigned status = (dw3 >> 17) & 0x7ff;
                    admin_cq_head = static_cast<unsigned short>((admin_cq_head + 1) % queue_depth);
                    if (admin_cq_head == 0)
                        admin_cq_phase ^= 1;
                    doorbell(0, true, admin_cq_head);
                    return status;
                }
                asm volatile("pause");
            }
            return ~0u;
        }

        bool identify() {
            command identify_controller{};
            identify_controller.dword[0] = admin_identify | (static_cast<unsigned>(admin_cid++) << 16);
            identify_controller.dword[10] = 1;
            submit_admin(identify_controller, identify_page.value.physical);
            if (poll_admin() != 0)
                return false;
            auto* data = static_cast<unsigned char*>(identify_page.value.virtual_address);
            const unsigned namespaces = *reinterpret_cast<unsigned*>(data + 516);
            if (namespaces == 0)
                return false;
            for (unsigned i = 0; i < page_size; ++i)
                data[i] = 0;
            command identify_namespace{};
            identify_namespace.dword[0] = admin_identify | (static_cast<unsigned>(admin_cid++) << 16);
            identify_namespace.dword[1] = namespace_id;
            submit_admin(identify_namespace, identify_page.value.physical);
            if (poll_admin() != 0)
                return false;
            const unsigned long long size = *reinterpret_cast<unsigned long long*>(data);
            const unsigned format = data[26] & 0xf;
            if (size == 0 || format >= 16)
                return false;
            const unsigned exponent = data[128 + format * 4 + 2];
            if (exponent < 9 || exponent > 12)
                return false;
            namespace_size = size;
            block_size = 1u << exponent;
            return true;
        }

        bool create_io_queues() {
            command cq{};
            cq.dword[0] = admin_create_io_cq | (static_cast<unsigned>(admin_cid++) << 16);
            cq.dword[10] = 1 | ((queue_depth - 1) << 16);
            cq.dword[11] = 1;
            submit_admin(cq, io_cq.value.physical);
            if (poll_admin() != 0)
                return false;
            command sq{};
            sq.dword[0] = admin_create_io_sq | (static_cast<unsigned>(admin_cid++) << 16);
            sq.dword[10] = 1 | ((queue_depth - 1) << 16);
            sq.dword[11] = 1 | (1u << 16);
            submit_admin(sq, io_sq.value.physical);
            return poll_admin() == 0;
        }

        bool start() {
            if (astia_module_pci_enable_bus_master(pci) != 0) {
                return false;
            }
            if (astia_module_pci_map_bar(pci, 0) != 0) {
                return false;
            }
            if (astia_module_pci_disable_msix(pci) != 0) {
                return false;
            }
            registers = reinterpret_cast<volatile unsigned char*>(
                astia_module_pci_bar_mapped(pci, 0));
            capability = read64(0);
            if (((capability >> 48) & 0xf) != 0) {
                return false;
            }
            if (!allocate_queues()) {
                return false;
            }
            if (!reset_and_enable()) {
                return false;
            }
            if (!identify()) {
                return false;
            }
            if (!create_io_queues()) {
                return false;
            }
            started = true;
            astia_module_log_u64("nvme", "namespace blocks", namespace_size);
            return true;
        }

        void submit_io(command& value) {
            auto* entries = static_cast<command*>(io_sq.value.virtual_address);
            entries[io_sq_tail] = value;
            io_sq_tail = static_cast<unsigned short>((io_sq_tail + 1) % queue_depth);
            doorbell(1, false, io_sq_tail);
        }

        unsigned poll_io() {
            auto* entries = static_cast<volatile completion*>(io_cq.value.virtual_address);
            for (unsigned spin = 0; spin < timeout_spins; ++spin) {
                const unsigned dw3 = entries[io_cq_head].dword[3];
                if (((dw3 >> 16) & 1) == io_cq_phase) {
                    const unsigned status = (dw3 >> 17) & 0x7ff;
                    io_cq_head = static_cast<unsigned short>((io_cq_head + 1) % queue_depth);
                    if (io_cq_head == 0)
                        io_cq_phase ^= 1;
                    doorbell(1, true, io_cq_head);
                    return status;
                }
                asm volatile("pause");
            }
            return ~0u;
        }

        long read(void* buffer, unsigned length, unsigned long long offset) {
            if (!started || length == 0 || length > 8192)
                return -5;
            uint64_t physical = 0;
            void* token = nullptr;
            if (astia_module_pin_user_write(buffer, length, &physical, &token) != 0)
                return -14;
            dma bounce;
            const bool direct = physical != ~0ull &&
                                length <= page_size - static_cast<unsigned>(physical & (page_size - 1));
            if (!direct) {
                astia_module_unpin(token);
                token = nullptr;
                if (astia_module_dma_alloc((length + page_size - 1) / page_size, 1,
                                            &bounce.value) != 0)
                    return -12;
                physical = bounce.value.physical;
            }
            command request{};
            const unsigned short cid = io_cid++;
            request.dword[0] = io_read | (static_cast<unsigned>(cid) << 16);
            request.dword[1] = namespace_id;
            request.dword[6] = static_cast<unsigned>(physical);
            request.dword[7] = static_cast<unsigned>(physical >> 32);
            request.dword[10] = static_cast<unsigned>(offset / block_size);
            request.dword[11] = static_cast<unsigned>((offset / block_size) >> 32);
            request.dword[12] = length / block_size - 1;
            submit_io(request);
            const unsigned status = poll_io();
            long result = status == 0 ? static_cast<long>(length) : -5;
            if (!direct && result > 0 &&
                astia_module_copy_out(buffer, length, bounce.value.virtual_address) != 0)
                result = -14;
            if (token != nullptr)
                astia_module_unpin(token);
            return result;
        }
    };

    controller device;

    int match(const astia_module_pci_device* value) {
        return value != nullptr && value->class_code == 1 && value->subclass == 8 &&
               value->programming_interface == 2 && value->bar_count != 0 &&
               value->bar_present[0] != 0 && value->bar_io[0] == 0;
    }

    int probe(const astia_module_pci_device* value, void** context) {
        if (value == nullptr || context == nullptr || device.started) {
            return 0;
        }
        device.pci = value->opaque;
        if (!device.start())
            return 0;
        *context = &device;
        return 1;
    }

    void remove(void*) {
        device.started = false;
    }

    uint64_t size(void* context) {
        return static_cast<controller*>(context)->namespace_size *
               static_cast<controller*>(context)->block_size;
    }

    unsigned block_size(void* context) {
        return static_cast<controller*>(context)->block_size;
    }

    long read(void* context, void* buffer, unsigned length, uint64_t offset) {
        return static_cast<controller*>(context)->read(buffer, length, offset);
    }

    int selftest() {
        char sector[512]{};
        if (!device.started)
            return 1;
        if (astia_module_read_registered("/dev/nvme0n1", sector, sizeof sector, 0) != 0)
            return 0;
        const bool valid = sector[0] == 'A' && sector[1] == 'S' && sector[2] == 'T' &&
                           sector[3] == 'I' && sector[4] == 'A' && sector[5] == '_' &&
                           sector[6] == 'N' && sector[7] == 'V';
        if (valid)
            astia_module_log("nvme", "known sector ok");
        return valid ? 1 : 0;
    }

    const astia_module_pci_block_driver ops = {
        "nvme", "/dev/nvme0n1", match, probe, remove, size, block_size, read, selftest};

}

extern "C" int module_main() {
    return astia_module_register_pci_block_driver(&ops);
}
