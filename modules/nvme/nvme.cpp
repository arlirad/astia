#include <astia/module_abi.h>
#include <astia/mm/dma_buffer.hpp>
#include <astia/dev/blk/blkdev.hpp>
#include <astia/dev/driver.hpp>
#include <astia/hw/pci.hpp>
#include <astia/dev/device_file.hpp>
#include <astia/fs.hpp>
#include <astia/io.hpp>
#include <astia/mm/share.hpp>
#include "nvme_protocol.hpp"

namespace {

    constexpr unsigned queue_depth = 16;
    constexpr unsigned timeout_spins = 4000000;
    constexpr unsigned admin_identify = 0x06;
    constexpr unsigned admin_create_io_cq = 0x05;
    constexpr unsigned admin_create_io_sq = 0x01;
    constexpr unsigned io_flush = 0x00;
    constexpr unsigned io_write = 0x01;
    constexpr unsigned io_read = 0x02;
    constexpr unsigned max_async_requests = 4;
    constexpr unsigned max_namespaces = 16;
    constexpr unsigned csts_fatal = 1u << 1;

    using dma = astia::mm::dma_buffer;

    using command = nvme_protocol::command;

    struct completion {
        unsigned dword[4]{};
    };

    struct controller;

    struct namespace_device {
        controller* owner = nullptr;
        unsigned id = 0;
        unsigned long long size = 0;
        unsigned block_size = 0;
        char path[32]{};
    };

    struct async_request {
        bool used = false;
        bool canceled = false;
        unsigned short cid = 0;
        astia::io::request* request = nullptr;
        long result = 0;
        bool completed = false;
        bool hardware_done = false;
        dma bounce;
    };

    struct controller {
        void* pci = nullptr;
        volatile unsigned char* registers = nullptr;
        unsigned long long capability = 0;
        unsigned doorbell_stride = 4;
        unsigned namespace_count = 0;
        namespace_device namespaces[max_namespaces]{};
        unsigned short admin_sq_tail = 0;
        unsigned short admin_cq_head = 0;
        unsigned short io_sq_tail = 0;
        unsigned short io_cq_head = 0;
        unsigned char admin_cq_phase = 1;
        unsigned char io_cq_phase = 1;
        unsigned short admin_cid = 1;
        unsigned short io_cid = 1;
        bool started = false;
        bool recovering = false;
        bool msix_active = false;
        bool quiescing = false;
        bool release_pending = false;
        volatile bool interrupt_pending = false;
        volatile bool servicing_async = false;
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

        bool wait_ready(bool ready, bool reject_fatal = true) const {
            for (unsigned spin = 0; spin < timeout_spins; ++spin) {
                const bool observed = (read32(0x1c) & 1) != 0;
                if (observed == ready)
                    return !reject_fatal || (read32(0x1c) & csts_fatal) == 0;
                asm volatile("pause");
            }
            return false;
        }

        bool fatal() const {
            return registers != nullptr && (read32(0x1c) & csts_fatal) != 0;
        }

        void reset_queue_state() {
            admin_sq_tail = 0;
            admin_cq_head = 0;
            io_sq_tail = 0;
            io_cq_head = 0;
            admin_cq_phase = 1;
            io_cq_phase = 1;
            admin_cid = 1;
            io_cid = 1;
            interrupt_pending = false;
            dma* queues[] = {&admin_sq, &admin_cq, &io_sq, &io_cq};
            for (dma* queue : queues) {
                if (!static_cast<bool>(*queue))
                    continue;
                auto* bytes = static_cast<volatile unsigned char*>(
                    queue->data());
                const uint64_t size = queue->pages() * ASTIA_PAGE_SIZE;
                for (uint64_t i = 0; i < size; ++i)
                    bytes[i] = 0;
            }
        }

        bool controller_reset() {
            if (registers == nullptr)
                return false;
            write32(0x14, 0);
            if (!wait_ready(false, false))
                return false;
            reset_queue_state();
            started = false;
            astia_module_log("nvme", fatal() ? "controller fatal" : "controller reset");
            return true;
        }

        void fail_async_requests(long result) {
            for (async_request& pending : async_requests) {
                if (!pending.used)
                    continue;
                if (!pending.completed) {
                    pending.result = result;
                    pending.completed = true;
                }
            }
        }

        void acknowledge_async_hardware() {
            for (async_request& pending : async_requests) {
                if (!pending.used || !pending.completed)
                    continue;
                pending.hardware_done = true;
                if (pending.request != nullptr && pending.request->device_state() != nullptr)
                    pending.request->wake();
            }
        }

        bool recover_controller() {
            if (recovering)
                return false;
            recovering = true;
            fail_async_requests(-5);
            const bool interrupts = msix_active;
            const bool masked = !interrupts || astia_module_pci_msix_disable(&msix_route) == 0;
            const bool reset = masked && controller_reset();
            if (reset)
                acknowledge_async_hardware();
            const bool ready = reset && reset_and_enable();
            const bool identified = ready && identify();
            const bool queues = identified && create_io_queues();
            const bool enabled = queues &&
                                 (!interrupts || (astia_module_pci_msix_enable(&msix_route) == 0 &&
                                                  astia_module_pci_msix_unmask(&msix_route) == 0));
            recovering = false;
            if (!masked || !enabled) {
                started = false;
                astia_module_log("nvme", "controller recovery failed");
                return false;
            }
            started = true;
            astia_module_log("nvme", "controller recovery ok");
            return true;
        }

        void abort_controller() {
            if (recovering) {
                started = false;
                return;
            }
            if (!recover_controller())
                started = false;
        }

        bool valid_transfer(const namespace_device& value, void* buffer, unsigned length,
                            unsigned long long offset) const {
            return value.owner == this &&
                   nvme_protocol::valid_transfer(
                       {started, value.size, value.block_size}, buffer, length, offset);
        }

        bool set_prp(command& value, uint64_t physical, unsigned length) {
            return nvme_protocol::set_prp(value, physical, length);
        }

        void set_address(unsigned offset, unsigned long long address) {
            write32(offset, static_cast<unsigned>(address));
            write32(offset + 4, static_cast<unsigned>(address >> 32));
        }

        bool allocate_queues() {
            if (admin_sq.allocate() && admin_cq.allocate() && io_sq.allocate() &&
                io_cq.allocate() && identify_page.allocate())
                return true;
            admin_sq.release();
            admin_cq.release();
            io_sq.release();
            io_cq.release();
            identify_page.release();
            return false;
        }

        bool reset_and_enable() {
            doorbell_stride = 4u << static_cast<unsigned>((capability >> 32) & 0xf);
            if ((read32(0x14) & 1) != 0)
                write32(0x14, 0);
            if (!wait_ready(false, false))
                return false;
            if (!nvme_protocol::valid_queue_depth(
                    queue_depth, static_cast<unsigned>((capability & 0xffffu) + 1u)))
                return false;
            write32(0x24, (queue_depth - 1) << 16 | (queue_depth - 1));
            set_address(0x28, admin_sq.physical());
            set_address(0x30, admin_cq.physical());
            write32(0x14, (4u << 20) | (6u << 16) | 1u);
            return wait_ready(true);
        }

        void submit_admin(command& value, unsigned long long data) {
            value.dword[6] = static_cast<unsigned>(data);
            value.dword[7] = static_cast<unsigned>(data >> 32);
            auto* entries = static_cast<command*>(admin_sq.data());
            entries[admin_sq_tail] = value;
            admin_sq_tail = static_cast<unsigned short>((admin_sq_tail + 1) % queue_depth);
            doorbell(0, false, admin_sq_tail);
        }

        unsigned poll_admin() {
            auto* entries = static_cast<volatile completion*>(admin_cq.data());
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
            abort_controller();
            return ~0u;
        }

        bool identify_namespace(namespace_device& value) {
            auto* data = static_cast<unsigned char*>(identify_page.data());
            for (unsigned i = 0; i < ASTIA_PAGE_SIZE; ++i)
                data[i] = 0;
            command identify_value{};
            identify_value.dword[0] =
                admin_identify | (static_cast<unsigned>(admin_cid++) << 16);
            identify_value.dword[1] = value.id;
            submit_admin(identify_value, identify_page.physical());
            if (poll_admin() != 0)
                return false;
            const unsigned long long size = *reinterpret_cast<unsigned long long*>(data);
            const unsigned format = data[26] & 0xf;
            if (size == 0 || format >= 16)
                return false;
            const unsigned exponent = data[128 + format * 4 + 2];
            if (exponent < 9 || exponent > 12)
                return false;
            const unsigned bytes = 1u << exponent;
            if (size > ~uint64_t{0} / bytes)
                return false;
            value.owner = this;
            value.size = size;
            value.block_size = bytes;
            return true;
        }

        bool make_path(namespace_device& value) {
            constexpr char prefix[] = "/dev/nvme0n";
            unsigned prefix_length = 0;
            while (prefix[prefix_length] != '\0') {
                value.path[prefix_length] = prefix[prefix_length];
                ++prefix_length;
            }
            unsigned char digits[10]{};
            unsigned count = 0;
            unsigned id = value.id;
            do {
                digits[count++] = static_cast<unsigned char>('0' + id % 10);
                id /= 10;
            } while (id != 0 && count < sizeof(digits));
            if (id != 0 || prefix_length + count + 1 > sizeof(value.path))
                return false;
            for (unsigned i = 0; i < count; ++i)
                value.path[prefix_length + i] = static_cast<char>(digits[count - i - 1]);
            value.path[prefix_length + count] = '\0';
            return true;
        }

        bool identify() {
            auto* data = static_cast<unsigned char*>(identify_page.data());
            for (unsigned i = 0; i < ASTIA_PAGE_SIZE; ++i)
                data[i] = 0;
            command identify_controller{};
            identify_controller.dword[0] =
                admin_identify | (static_cast<unsigned>(admin_cid++) << 16);
            identify_controller.dword[10] = 1;
            submit_admin(identify_controller, identify_page.physical());
            if (poll_admin() != 0)
                return false;
            const unsigned max_namespace_id = *reinterpret_cast<unsigned*>(data + 516);
            if (max_namespace_id == 0)
                return false;

            namespace_count = 0;
            unsigned cursor = 0;
            for (;;) {
                for (unsigned i = 0; i < ASTIA_PAGE_SIZE; ++i)
                    data[i] = 0;
                command list{};
                list.dword[0] = admin_identify | (static_cast<unsigned>(admin_cid++) << 16);
                list.dword[1] = cursor;
                list.dword[10] = 2;
                submit_admin(list, identify_page.physical());
                if (poll_admin() != 0)
                    return false;
                unsigned ids[ASTIA_PAGE_SIZE / sizeof(unsigned)]{};
                unsigned entries = 0;
                unsigned previous = cursor;
                for (; entries < ASTIA_PAGE_SIZE / sizeof(unsigned); ++entries) {
                    const unsigned id = *reinterpret_cast<unsigned*>(data + entries * 4);
                    if (id == 0)
                        break;
                    if (id <= previous || id > max_namespace_id ||
                        namespace_count + entries >= max_namespaces)
                        return false;
                    ids[entries] = id;
                    previous = id;
                }
                for (unsigned i = 0; i < entries; ++i) {
                    namespace_device& value = namespaces[namespace_count++];
                    value = {};
                    value.id = ids[i];
                    if (!identify_namespace(value) || !make_path(value))
                        return false;
                    cursor = value.id;
                }
                if (entries < ASTIA_PAGE_SIZE / sizeof(unsigned))
                    break;
                if (cursor == 0)
                    return false;
            }
            return namespace_count != 0;
        }

        bool flush(namespace_device& value) {
            if (!started || value.owner != this)
                return false;
            command request{};
            request.dword[0] = io_flush | (static_cast<unsigned>(io_cid++) << 16);
            request.dword[1] = value.id;
            submit_io(request);
            return poll_io() == 0;
        }

        bool create_io_queues() {
            command cq{};
            cq.dword[0] = admin_create_io_cq | (static_cast<unsigned>(admin_cid++) << 16);
            cq.dword[10] = 1 | ((queue_depth - 1) << 16);
            cq.dword[11] = 1 | (msix_active ? (2u | (msix_route.table_index << 16)) : 0);
            submit_admin(cq, io_cq.physical());
            if (poll_admin() != 0)
                return false;
            command sq{};
            sq.dword[0] = admin_create_io_sq | (static_cast<unsigned>(admin_cid++) << 16);
            sq.dword[10] = 1 | ((queue_depth - 1) << 16);
            sq.dword[11] = 1 | (1u << 16);
            submit_admin(sq, io_sq.physical());
            return poll_admin() == 0;
        }

        int submit_async(namespace_device& value, astia::io::request& request) {
            if (quiescing || !started)
                return -19;
            if (request.direction() != astia::io::direction::input &&
                request.direction() != astia::io::direction::output)
                return -22;
            if (!valid_transfer(value, request.buffer(), request.length(), request.offset()))
                return -22;
            for (async_request& pending : async_requests) {
                if (pending.used)
                    continue;
                pending = {};
                if (!pending.bounce.allocate(nvme_protocol::page_count(request.length())))
                    return -12;
                pending.used = true;
                pending.request = &request;
                pending.hardware_done = false;
                request.poll_interval_ns(1000000);
                request.timeout_ns(1000000);
                pending.cid = io_cid++;
                const bool write = request.direction() == astia::io::direction::output;
                if (write) {
                    auto* source = static_cast<const unsigned char*>(request.buffer());
                    auto* target = static_cast<unsigned char*>(
                        pending.bounce.data());
                    for (unsigned i = 0; i < request.length(); ++i)
                        target[i] = source[i];
                }
                command command_value{};
                command_value.dword[0] = (write ? io_write : io_read) |
                                          (static_cast<unsigned>(pending.cid) << 16);
                command_value.dword[1] = value.id;
                if (!set_prp(command_value, pending.bounce.physical(), request.length())) {
                    pending.bounce.release();
                    pending = {};
                    return -22;
                }
                command_value.dword[10] = static_cast<unsigned>(request.offset() / value.block_size);
                command_value.dword[11] =
                    static_cast<unsigned>((request.offset() / value.block_size) >> 32);
                command_value.dword[12] = (write ? (1u << 30) : 0) |
                                          (request.length() / value.block_size - 1);
                submit_io(command_value);
                return 0;
            }
            return -16;
        }

        int cancel_async(astia::io::request& request, bool retain) {
            for (async_request& pending : async_requests) {
                if (pending.used && pending.request == &request) {
                    pending.canceled = true;
                    pending.result = -125;
                    pending.completed = true;
                    if (!retain)
                        pending.request = nullptr;
                    else if (pending.request->device_state() != nullptr)
                        pending.request->wake();
                    return 0;
                }
            }
            return -2;
        }

        int poll_async(astia::io::request& request) {
            if (quiescing)
                return 0;
            service_async();
            for (async_request& pending : async_requests)
                if (pending.used && pending.request == &request) {
                    if (request.timeout_expired())
                        request.timeout_expired(false);
                    return 0;
                }
            return 0;
        }

        bool complete_async(astia::io::request& request, long& result) {
            for (async_request& pending : async_requests) {
                if (!pending.used || pending.request != &request || !pending.completed ||
                    !pending.hardware_done)
                    continue;
                result = pending.result;
                pending.bounce.release();
                pending = {};
                request.cancel_acknowledge();
                if (release_pending)
                    release();
                return true;
            }
            return false;
        }

        void service_async() {
            if (quiescing || recovering)
                return;
            if (__atomic_exchange_n(&servicing_async, true, __ATOMIC_ACQUIRE))
                return;
            struct service_guard {
                volatile bool& busy;
                ~service_guard() { __atomic_store_n(&busy, false, __ATOMIC_RELEASE); }
            } guard{servicing_async};
            auto* entries = static_cast<volatile completion*>(io_cq.data());
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
                if (found == nullptr)
                    return;
                const unsigned status = (dw3 >> 17) & 0x7ff;
                if (!found->canceled && found->request != nullptr && !found->completed) {
                    long result = status == 0 ? static_cast<long>(found->request->length()) : -5;
                    if (status == 0 && found->request->direction() == astia::io::direction::input) {
                        auto* destination = static_cast<unsigned char*>(found->request->buffer());
                        auto* source = static_cast<const unsigned char*>(
                            found->bounce.data());
                        for (unsigned i = 0; i < found->request->length(); ++i)
                            destination[i] = source[i];
                    }
                    found->result = result;
                    found->completed = true;
                    found->hardware_done = true;
                    if (found->request->device_state() != nullptr)
                        found->request->wake();
                }
                if (found->canceled) {
                    found->hardware_done = true;
                    if (found->request != nullptr) {
                        if (found->request->device_state() != nullptr)
                            found->request->wake();
                    } else {
                        found->bounce.release();
                        *found = {};
                    }
                }
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
            quiescing = false;
            if (astia_module_pci_enable_bus_master(pci) != 0 ||
                astia_module_pci_map_bar(pci, 0) != 0 ||
                astia_module_pci_disable_msix(pci) != 0)
                return false;
            registers = reinterpret_cast<volatile unsigned char*>(
                astia_module_pci_bar_mapped(pci, 0));
            capability = read64(0);
            if (((capability >> 48) & 0xf) != 0 || !allocate_queues() || !reset_and_enable() ||
                !identify())
                return false;
            if (!setup_msix()) {
                (void) astia_module_pci_disable_msix(pci);
                astia_module_log("nvme", "polling fallback");
            }
            if (!create_io_queues())
                return false;
            if (msix_active) {
                if (astia_module_pci_msix_unmask(&msix_route) != 0)
                    return false;
                astia_module_log("nvme", "msix enabled");
            }
            started = true;
            astia_module_log_u64("nvme", "namespace count", namespace_count);
            astia_module_log_u64("nvme", "namespace blocks", namespaces[0].size);
            return true;
        }

        void submit_io(command& value) {
            auto* entries = static_cast<command*>(io_sq.data());
            entries[io_sq_tail] = value;
            io_sq_tail = static_cast<unsigned short>((io_sq_tail + 1) % queue_depth);
            doorbell(1, false, io_sq_tail);
        }

        unsigned poll_io() {
            auto* entries = static_cast<volatile completion*>(io_cq.data());
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
            abort_controller();
            return ~0u;
        }

        long read(namespace_device& value, void* buffer, unsigned length,
                  unsigned long long offset) {
            if (!valid_transfer(value, buffer, length, offset))
                return -5;
            uint64_t physical = 0;
            void* token = nullptr;
            if (astia_module_pin_user_write(buffer, length, &physical, &token) != 0)
                return -14;
            dma bounce;
            const bool direct = nvme_protocol::fits_one_page(physical, length);
            if (!direct) {
                astia_module_unpin(token);
                token = nullptr;
                if (!bounce.allocate(nvme_protocol::page_count(length)))
                    return -12;
                physical = bounce.physical();
            }
            command request{};
            const unsigned short cid = io_cid++;
            request.dword[0] = io_read | (static_cast<unsigned>(cid) << 16);
            request.dword[1] = value.id;
            if (!set_prp(request, physical, length)) {
                if (token != nullptr)
                    astia_module_unpin(token);
                return -22;
            }
            request.dword[10] = static_cast<unsigned>(offset / value.block_size);
            request.dword[11] = static_cast<unsigned>((offset / value.block_size) >> 32);
            request.dword[12] = length / value.block_size - 1;
            submit_io(request);
            const unsigned status = poll_io();
            long result = status == 0 ? static_cast<long>(length) : -5;
            if (!direct && result > 0 &&
                astia_module_copy_out(buffer, length, bounce.data()) != 0)
                result = -14;
            if (token != nullptr)
                astia_module_unpin(token);
            return result;
        }

        long write(namespace_device& value, const void* buffer, unsigned length,
                   unsigned long long offset) {
            if (!valid_transfer(value, const_cast<void*>(buffer), length, offset))
                return -22;
            uint64_t physical = 0;
            void* token = nullptr;
            if (astia_module_pin_user_read(buffer, length, &physical, &token) != 0)
                return -14;
            dma bounce;
            const bool direct = nvme_protocol::fits_one_page(physical, length);
            if (!direct) {
                astia_module_unpin(token);
                token = nullptr;
                if (!bounce.allocate(nvme_protocol::page_count(length)))
                    return -12;
                auto* source = static_cast<const unsigned char*>(buffer);
                auto* target = static_cast<unsigned char*>(bounce.data());
                for (unsigned i = 0; i < length; ++i)
                    target[i] = source[i];
                physical = bounce.physical();
            }
            command request{};
            const unsigned short cid = io_cid++;
            request.dword[0] = io_write | (static_cast<unsigned>(cid) << 16);
            request.dword[1] = value.id;
            if (!set_prp(request, physical, length)) {
                if (token != nullptr)
                    astia_module_unpin(token);
                return -22;
            }
            request.dword[10] = static_cast<unsigned>(offset / value.block_size);
            request.dword[11] = static_cast<unsigned>((offset / value.block_size) >> 32);
            request.dword[12] = (1u << 30) | (length / value.block_size - 1);
            submit_io(request);
            const unsigned status = poll_io();
            if (token != nullptr)
                astia_module_unpin(token);
            return status == 0 ? static_cast<long>(length) : -5;
        }

        void quiesce() {
            if (quiescing)
                return;
            quiescing = true;
            if (registers != nullptr && (read32(0x14) & 1) != 0) {
                write32(0x14, 0);
                (void) wait_ready(false);
            }
            if (msix_active) {
                (void) astia_module_pci_msix_release(&msix_route);
                msix_active = false;
            }
            while (__atomic_load_n(&servicing_async, __ATOMIC_ACQUIRE))
                asm volatile("pause");
            fail_async_requests(-19);
            acknowledge_async_hardware();
            for (async_request& pending : async_requests) {
                if (!pending.used || pending.request != nullptr)
                    continue;
                pending.bounce.release();
                pending = {};
            }
        }

        void release() {
            for (const async_request& pending : async_requests)
                if (pending.used) {
                    release_pending = true;
                    return;
                }
            release_pending = false;
            admin_sq.release();
            admin_cq.release();
            io_sq.release();
            io_cq.release();
            identify_page.release();
            registers = nullptr;
            started = false;
            namespace_count = 0;
        }

        void stop() {
            quiesce();
            release();
        }
    };

#ifndef ASTIA_TEARDOWN_TEST
    controller device;

    bool known(const char* value, const char* expected, unsigned length) {
        for (unsigned i = 0; i < length; ++i)
            if (value[i] != expected[i])
                return false;
        return true;
    }

    class namespace_block final : public astia::dev::blk::blkdev {
        public:
        namespace_block(namespace_device& value)
            : blkdev("nvme", value.size * value.block_size, value.block_size), _value(value) {}

        protected:
        long read_blocks(void* buffer, astia::u32 length, astia::u64 offset) override {
            return _value.owner->read(_value, buffer, length, offset);
        }

        long write_blocks(const void* buffer, astia::u32 length, astia::u64 offset) override {
            return _value.owner->write(_value, buffer, length, offset);
        }

        long submit(astia::io::request& request) override {
            const long valid = validate_request(request);
            if (valid != 0)
                return valid;
            if (request.device_state() == this) {
                _value.owner->poll_async(request);
                long result = 0;
                if (!_value.owner->complete_async(request, result))
                    return 0;
                request.device_state(nullptr);
                request.cancel_handler_set(nullptr);
                return result;
            }
            if (request.device_state() != nullptr)
                return -16;
            request.device_state(this);
            request.cancel_handler_set(cancel_request);
            const int result = _value.owner->submit_async(_value, request);
            if (result < 0) {
                request.device_state(nullptr);
                request.cancel_handler_set(nullptr);
                return result;
            }
            return 0;
        }

        private:
        static void cancel_request(astia::io::request& request) {
            auto* self = static_cast<namespace_block*>(request.device_state());
            if (self != nullptr) {
                const bool retain = request.cancellation_retain();
                (void) self->_value.owner->cancel_async(request, retain);
                if (!retain) {
                    request.device_state(nullptr);
                    request.cancel_handler_set(nullptr);
                }
            }
        }

        namespace_device& _value;
    };

    class nvme_group final : public astia::dev::device {
        public:
        nvme_group() : device("nvme", astia::dev::dv_sync) {}
        bool add(const astia::mm::shptr<namespace_block>& value) {
            if (!value || _count == max_namespaces)
                return false;
            _blocks[_count++] = value;
            return true;
        }
        long read(void*, astia::u32, astia::u64) override { return -19; }
        long write(const void*, astia::u32, astia::u64) override { return -30; }

        private:
        astia::mm::shptr<namespace_block> _blocks[max_namespaces]{};
        astia::u32 _count = 0;
    };

    class nvme_driver final : public astia::hw::driver {
        public:
        const char* name() const override { return "nvme"; }

        bool match_pci(const astia::hw::pci_device& value) const {
            return value.class_code == 1 && value.subclass == 8 &&
                   value.programming_interface == 2 && value.bar_count != 0 &&
                   value.bars[0].present && !value.bars[0].io;
        }

        bool probe_pci(astia::hw::pci_device& value) {
            if (device.started)
                return false;
            device.pci = &value;
            if (!device.start()) {
                device.stop();
                return false;
            }
            return true;
        }

        bool match(const astia::hw::device_info& info) const override {
            const auto* pci = astia::hw::pci_device_info::get(info);
            return pci != nullptr && match_pci(pci->device());
        }

        bool probe(
                const astia::hw::device_info& info) override {
            const auto* pci = astia::hw::pci_device_info::get(info);
            if (pci == nullptr)
                return {};
            auto* stable = astia::hw::pci_devices().find(pci->device().bdf);
            if (stable == nullptr || !probe_pci(*stable))
                return {};
            auto group = astia::mm::share<nvme_group>();
            if (!group) {
                device.stop();
                return {};
            }
            for (astia::u32 i = 0; i < device.namespace_count; ++i) {
                auto block = astia::mm::share<namespace_block>(device.namespaces[i]);
                if (!block || !group->add(block)) {
                    rollback_probe();
                    return {};
                }
                astia::mm::shptr<astia::dev::device> owner = block;
                auto file = astia::mm::share<astia::dev::device_file>(owner);
                if (!file) {
                    rollback_probe();
                    return {};
                }
                if (astia::fs::register_device(device.namespaces[i].path, file) < 0) {
                    rollback_probe();
                    return {};
                }
                _files[i] = file;
                ++_file_count;
            }
            _product = group;
            return true;
        }

        astia::hw::product_handle publish(const astia::hw::device_info&) override {
            return _product;
        }

        void remove(const astia::hw::device_info&) override {
            device.quiesce();
            unregister_nodes();
            device.release();
            _product = {};
        }

        bool selftest() override {
            char first[512]{};
            char second[512]{};
            if (!device.started || device.namespace_count < 2)
                return false;
            if (device.msix_active) {
                astia_module_msix_route copied = device.msix_route;
                if (astia_module_pci_msix_program(&copied) != -22 ||
                    astia_module_pci_msix_release(&copied) != -22 ||
                    astia_module_pci_msix_release(&copied) != -22)
                    return false;
                copied = device.msix_route;
                copied.pci_bus ^= 1;
                if (astia_module_pci_msix_enable(&copied) != -22)
                    return false;
                copied = device.msix_route;
                copied.table_offset = ~0u;
                if (astia_module_pci_msix_program(&copied) != -22)
                    return false;
                astia_module_log("nvme", "msix handle validation ok");
            }
            const int first_read =
                astia_module_read_registered_async("/dev/nvme0n1", first, sizeof first, 0);
            const int second_read =
                astia_module_read_registered_async("/dev/nvme0n2", second, sizeof second, 0);
            if (first_read != 0 || second_read != 0)
                return false;
            astia_module_log("nvme", device.msix_active ? "async completion"
                                                           : "async polling fallback");
            char write_sector[512]{};
            for (unsigned i = 0; i < sizeof write_sector; ++i)
                write_sector[i] = static_cast<char>("NVME_WRITE_FUA_OK"[i % 17] ^ i);
            if (astia_module_write_registered_async("/dev/nvme0n1", write_sector,
                                                    sizeof write_sector, sizeof first) != 0)
                return false;
            if (!device.flush(device.namespaces[0]))
                return false;
            char reopened[512]{};
            if (astia_module_read_registered_async("/dev/nvme0n1", reopened, sizeof reopened,
                                                   sizeof first) != 0)
                return false;
            for (unsigned i = 0; i < sizeof reopened; ++i)
                if (reopened[i] != write_sector[i])
                    return false;
            astia_module_log("nvme", "write flush reopen ok");
            if (!device.recover_controller())
                return false;
            char reset_first[512]{};
            char reset_second[512]{};
            if (astia_module_read_registered_async("/dev/nvme0n1", reset_first,
                                                   sizeof reset_first, sizeof first) != 0 ||
                astia_module_read_registered_async("/dev/nvme0n2", reset_second,
                                                   sizeof reset_second, 0) != 0)
                return false;
            for (unsigned i = 0; i < sizeof reset_first; ++i)
                if (reset_first[i] != write_sector[i])
                    return false;
            if (!known(reset_second, "ASTIA_NVME_NAMESPACE_002", 24))
                return false;
            astia_module_log("nvme", "reset recovery persistence ok");
            char invalid[513]{};
            if (astia_module_read_registered_async("/dev/nvme0n1", invalid, sizeof invalid, 0) >= 0 ||
                astia_module_write_registered_async("/dev/nvme0n1", invalid, sizeof invalid, 0) >= 0 ||
                astia_module_read_registered_async("/dev/nvme0n3", invalid, sizeof first, 0) >= 0)
                return false;
            astia_module_log("nvme", "reset recovery error path");
            if (!known(first, "ASTIA_NVME_FIXTURE_SECTOR_000", 29) ||
                !known(second, "ASTIA_NVME_NAMESPACE_002", 24))
                return false;
            if (device.msix_active && device.interrupts_seen == 0)
                return false;
            astia_module_log("nvme", "namespace 1 ok");
            astia_module_log("nvme", "namespace 2 ok");
            astia_module_log("nvme", device.msix_active ? "msix interrupt path"
                                                           : "polling fallback");
            astia_module_log("nvme", "known sector ok");
            return true;
        }

        private:
        void unregister_nodes() {
            for (astia::u32 i = 0; i < _file_count; ++i)
                if (_files[i]) {
                    _files[i]->detach();
                    (void) astia::fs::unregister_device(device.namespaces[i].path, _files[i]);
                    _files[i] = {};
                }
            _file_count = 0;
        }

        void rollback_probe() {
            unregister_nodes();
            device.stop();
        }

        astia::mm::shptr<astia::dev::device> _product;
        astia::mm::shptr<astia::dev::device_file> _files[max_namespaces]{};
        astia::u32 _file_count = 0;

    };

    nvme_driver driver;

}

extern "C" int module_main() {
    return astia::hw::drivers().register_driver(driver) ? 0 : -22;
}
#endif
