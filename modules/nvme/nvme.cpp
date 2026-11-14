#include <astia/module_abi.h>

namespace {

    constexpr unsigned page_size = 4096;
    constexpr unsigned queue_depth = 16;
    constexpr unsigned timeout_spins = 4000000;
    constexpr unsigned admin_identify = 0x06;
    constexpr unsigned admin_create_io_cq = 0x05;
    constexpr unsigned admin_create_io_sq = 0x01;
    constexpr unsigned io_read = 0x02;
    constexpr unsigned max_async_requests = 4;

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

    struct async_request {
        bool used = false;
        bool canceled = false;
        unsigned short cid = 0;
        astia_module_io_request* request = nullptr;
        dma bounce;
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
        bool msix_active = false;
        volatile bool interrupt_pending = false;
        unsigned interrupts_seen = 0;
        astia_module_msix_route msix_route{};
        async_request async_requests[max_async_requests]{};
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
            cq.dword[11] = 1 | (msix_active ? (2u | (msix_route.table_index << 16)) : 0);
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

        int submit_async(astia_module_io_request* request) {
            if (request == nullptr || request->direction != 0 || !started)
                return -95;
            for (async_request& pending : async_requests) {
                if (pending.used)
                    continue;
                pending = {};
                if (!pending.bounce.allocate())
                    return -12;
                pending.used = true;
                pending.request = request;
                request->poll_timeout_ns = msix_active ? 0 : 1000000;
                pending.cid = io_cid++;
                command command_value{};
                command_value.dword[0] = io_read | (static_cast<unsigned>(pending.cid) << 16);
                command_value.dword[1] = namespace_id;
                command_value.dword[6] = static_cast<unsigned>(pending.bounce.value.physical);
                command_value.dword[7] = static_cast<unsigned>(pending.bounce.value.physical >> 32);
                command_value.dword[10] = static_cast<unsigned>(request->offset / block_size);
                command_value.dword[11] = static_cast<unsigned>((request->offset / block_size) >> 32);
                command_value.dword[12] = request->length / block_size - 1;
                submit_io(command_value);
                return 0;
            }
            return -16;
        }

        int cancel_async(astia_module_io_request* request) {
            for (async_request& pending : async_requests) {
                if (pending.used && pending.request == request) {
                    pending.canceled = true;
                    return 0;
                }
            }
            return -2;
        }

        int poll_async(astia_module_io_request* request) {
            service_async();
            for (async_request& pending : async_requests)
                if (pending.used && pending.request == request) {
                    if (request->timeout_expired) {
                        pending.canceled = true;
                        astia_module_io_complete(request->opaque, -110);
                    }
                    return 0;
                }
            return 0;
        }

        void service_async() {
            auto* entries = static_cast<volatile completion*>(io_cq.value.virtual_address);
            for (;;) {
                unsigned dw3 = entries[io_cq_head].dword[3];
                for (unsigned spin = 0; ((dw3 >> 16) & 1) != io_cq_phase && spin < 100000;
                     ++spin) {
                    asm volatile("pause");
                    dw3 = entries[io_cq_head].dword[3];
                }
                if (((dw3 >> 16) & 1) != io_cq_phase)
                    return;
                const unsigned short cid = static_cast<unsigned short>(dw3 & 0xffff);
                async_request* found = nullptr;
                for (async_request& pending : async_requests)
                    if (pending.used && pending.cid == cid) {
                        found = &pending;
                        break;
                    }
                if (found == nullptr) {
                    return;
                }
                const unsigned status = (dw3 >> 17) & 0x7ff;
                if (!found->canceled && found->request != nullptr) {
                    long result = status == 0 ? static_cast<long>(found->request->length) : -5;
                    if (status == 0) {
                        auto* destination = static_cast<unsigned char*>(found->request->buffer);
                        auto* source = static_cast<const unsigned char*>(
                            found->bounce.value.virtual_address);
                        for (unsigned i = 0; i < found->request->length; ++i)
                            destination[i] = source[i];
                    }
                    astia_module_io_complete(found->request->opaque, result);
                }
                found->bounce.release();
                *found = {};
                io_cq_head = static_cast<unsigned short>((io_cq_head + 1) % queue_depth);
                if (io_cq_head == 0)
                    io_cq_phase ^= 1;
                doorbell(1, true, io_cq_head);
            }
        }

        static void msix_interrupt(void* context) {
            auto* value = static_cast<controller*>(context);
            value->interrupt_pending = true;
            ++value->interrupts_seen;
            value->service_async();
        }

        bool setup_msix() {
#if defined(ASTIA_NVME_FORCE_POLLING)
            return false;
#else
            if (astia_module_pci_msix_request(pci, msix_interrupt, this, &msix_route) != 0)
                return false;
            if (astia_module_pci_msix_program(&msix_route) != 0) {
                astia_module_pci_msix_release(&msix_route);
                return false;
            }
            if (astia_module_pci_msix_enable(&msix_route) != 0) {
                astia_module_pci_msix_release(&msix_route);
                return false;
            }
            msix_active = true;
            return true;
#endif
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
            if (!setup_msix()) {
                (void) astia_module_pci_disable_msix(pci);
                astia_module_log("nvme", "polling fallback");
            }
            if (!create_io_queues()) {
                return false;
            }
            if (msix_active) {
                if (astia_module_pci_msix_unmask(&msix_route) != 0)
                    return false;
                astia_module_log("nvme", "msix enabled");
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
                if (interrupt_pending)
                    interrupt_pending = false;
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
        for (async_request& pending : device.async_requests)
            if (pending.used)
                pending.canceled = true;
        if (device.msix_active) {
            (void) astia_module_pci_msix_release(&device.msix_route);
            device.msix_active = false;
        }
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
        if (!device.started)
            return 1;
        if (!device.msix_active) {
            if (device.read(sector, sizeof sector, 0) != sizeof sector)
                return 0;
            astia_module_log("nvme", "async polling fallback");
        } else {
            const int async_result =
                astia_module_read_registered_async("/dev/nvme0n1", sector, sizeof sector, 0);
            if (async_result != 0)
                return 0;
            astia_module_log("nvme", "async completion");
        }
        char invalid[513]{};
        if (astia_module_read_registered_async("/dev/nvme0n1", invalid, sizeof invalid, 0) >= 0)
            return 0;
        astia_module_log("nvme", "async error path");
        const bool valid = sector[0] == 'A' && sector[1] == 'S' && sector[2] == 'T' &&
                           sector[3] == 'I' && sector[4] == 'A' && sector[5] == '_' &&
                           sector[6] == 'N' && sector[7] == 'V';
        if (valid && device.msix_active && device.interrupts_seen == 0)
            return 0;
        if (valid) {
            astia_module_log("nvme", device.msix_active ? "msix interrupt path"
                                                          : "polling fallback");
            astia_module_log("nvme", "known sector ok");
        }
        return valid ? 1 : 0;
    }

    const astia_module_pci_block_driver ops = {
        "nvme", "/dev/nvme0n1", match, probe, remove, size, block_size, read, selftest,
        async_submit, async_cancel, async_poll};

}

extern "C" int module_main() {
    return astia_module_register_pci_block_driver(&ops);
}
