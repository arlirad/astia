#include "nvme.hpp"

#include "../blk/block_device.hpp"
#include "../device_file.hpp"
#include "../pci.hpp"
#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/core/endian.hpp>
#include <astia/fs.hpp>
#include <astia/log.hpp>
#include <astia/memory.hpp>
#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/iovec.hpp>
#include <astia/mm/share.hpp>
#include <astia/span.hpp>
#include "../../sys/user_mem.hpp"
#include "../../proc/scheduler.hpp"

namespace astia::dev::nvme {

    namespace {

        using logs = astia::log::context<"nvme">;

        constexpr u8  admin_identify = 0x06;
        constexpr u8  admin_create_io_cq = 0x05;
        constexpr u8  admin_create_io_sq = 0x01;
        constexpr u8  io_read = 0x02;
        constexpr u32 controller_ready = 1u;
        constexpr u32 queue_depth = 16;
        constexpr u32 timeout_spins = 4000000;

        struct dma_buffer {
            u64 physical = mm::frame_allocator::invalid_frame;
            void* virtual_address = nullptr;
            usize pages = 0;

            bool allocate(usize count, bool zero = true) {
                if (count == 0 || count > ~usize{0} / mm::page_size)
                    return false;
                physical = mm::kernel_frames().alloc(
                    count, zero ? mm::frame_flags::zeroed : mm::frame_flags::none);
                if (physical == mm::frame_allocator::invalid_frame)
                    return false;
                virtual_address = mm::kernel_frames().to_virt(physical);
                pages = count;
                return virtual_address != nullptr;
            }

            void release() {
                if (physical != mm::frame_allocator::invalid_frame)
                    mm::kernel_frames().release(physical, pages);
                physical = mm::frame_allocator::invalid_frame;
                virtual_address = nullptr;
                pages = 0;
            }

            ~dma_buffer() {
                release();
            }

            dma_buffer() = default;
            dma_buffer(const dma_buffer&) = delete;
            dma_buffer& operator=(const dma_buffer&) = delete;
        };

        struct command {
            core::le<u32> dword[16]{};
        };

        struct completion {
            core::le<u32> dword[4]{};
        };

        class controller final : public blk::block_device {
            public:
            controller() : block_device("nvme0n1", 0, 512) {}

            bool start(pci_device& device) {
                if (started_ || device.class_code != 0x01 || device.subclass != 0x08 ||
                    device.programming_interface != 0x02 || device.bar_count == 0 ||
                    !device.bars[0].present || device.bars[0].io)
                    return false;
                if (!pci_enable_bus_master(device)) {
                    return false;
                }
                if (!pci_map_bar(device, 0)) {
                    return false;
                }
                disable_msix(device);
                registers_ = reinterpret_cast<volatile u8*>(device.bars[0].mapped);
                cap_ = read64(0x00);
                const u32 mps_min = static_cast<u32>((cap_ >> 48) & 0xfu);
                const bool queues = allocate_queues();
                if (mps_min != 0 || !queues)
                    return false;
                const bool enabled = reset_and_enable();
                if (!enabled)
                    return false;
                return true;
            }

            bool initialize_namespace() {
                if (started_)
                    return true;
                const bool identified = identify();
                if (!identified)
                    return false;
                const bool io_queues = create_io_queues();
                if (!io_queues)
                    return false;
                started_ = true;
                configure_geometry(namespace_size_ * block_size_, block_size_);
                logs::info("namespace blocks=%llu block-size=%u",
                           static_cast<unsigned long long>(namespace_size_), block_size_);
                return true;
            }

            long read_blocks(void* buffer, u32 length, u64 offset) override {
                if (!started_ || length == 0 || length > 8192)
                    return -EIO;
                mm::iovec pinned;
                if (sys::pin_user_span(span<u8>(static_cast<u8*>(buffer), length),
                                       io_direction::input, pinned) < 0)
                    return -EFAULT;

                dma_buffer bounce;
                u64 physical = pinned.physical();
                bool direct = physical != mm::frame_allocator::invalid_frame;
                const u64 page_offset = physical & mm::page_mask;
                if (!direct || length > mm::page_size - page_offset + mm::page_size) {
                    pinned.release();
                    const usize pages = (length + mm::page_size - 1) / mm::page_size;
                    if (!bounce.allocate(pages))
                        return -ENOMEM;
                    physical = bounce.physical;
                }

                command request{};
                const u16 cid = io_cid_++;
                request.dword[0] = io_read | (static_cast<u32>(cid) << 16);
                request.dword[1] = namespace_id_;
                request.dword[6] = static_cast<u32>(physical);
                request.dword[7] = static_cast<u32>(physical >> 32);
                if (length > mm::page_size - (physical & mm::page_mask)) {
                    const u64 second = (physical & ~mm::page_mask) + mm::page_size;
                    request.dword[8] = static_cast<u32>(second);
                    request.dword[9] = static_cast<u32>(second >> 32);
                }
                const u64 block = offset / block_size_;
                request.dword[10] = static_cast<u32>(block);
                request.dword[11] = static_cast<u32>(block >> 32);
                request.dword[12] = (length / block_size_) - 1;
                submit_io(request);
                const u32 status = poll_io();
                if (status != 0) {
                    pinned.release();
                    return -EIO;
                }
                if (!direct && !sys::copy_out(span<u8>::bytes(reinterpret_cast<u64>(buffer),
                                                               length),
                                              bounce.virtual_address)) {
                    pinned.release();
                    return -EFAULT;
                }
                pinned.release();
                return length;
            }

            private:
            volatile u8* registers_ = nullptr;
            u64 cap_ = 0;
            bool started_ = false;
            u16 admin_sq_tail_ = 0;
            u16 admin_cq_head_ = 0;
            u16 io_sq_tail_ = 0;
            u16 io_cq_head_ = 0;
            u8 admin_cq_phase_ = 1;
            u8 io_cq_phase_ = 1;
            u16 admin_cid_ = 1;
            u16 io_cid_ = 1;
            u32 doorbell_stride_ = 4;
            u32 namespace_id_ = 1;
            u64 namespace_size_ = 0;
            u32 block_size_ = 512;
            dma_buffer admin_sq_;
            dma_buffer admin_cq_;
            dma_buffer io_sq_;
            dma_buffer io_cq_;
            dma_buffer identify_page_;

            u32 read32(u32 offset) const {
                u8 bytes[sizeof(u32)]{};
                for (usize index = 0; index < sizeof(u32); ++index)
                    bytes[index] = registers_[offset + index];
                return core::le<u32>::load(bytes);
            }

            u64 read64(u32 offset) const {
                const u64 low = read32(offset);
                const u64 high = read32(offset + 4);
                return low | (high << 32);
            }

            void write32(u32 offset, u32 value) {
                u8 bytes[sizeof(u32)]{};
                core::le<u32>(value).store(bytes);
                for (usize index = 0; index < sizeof(u32); ++index)
                    registers_[offset + index] = bytes[index];
            }

            void disable_msix(pci_device& device) {
                if (!device.interrupt.msix)
                    return;
                u16 control = 0;
                if (!pci_config_read16(device.bdf, device.interrupt.capability + 2, control))
                    return;
                (void) pci_config_write16(device.bdf, device.interrupt.capability + 2,
                                          static_cast<u16>(control & ~(1u << 15)));
            }

            void doorbell(u16 queue, bool completion, u16 value) {
                const u32 index = static_cast<u32>(queue) * 2u + (completion ? 1u : 0u);
                write32(0x1000 + index * doorbell_stride_, value);
            }

            bool wait_ready(bool ready) const {
                for (u32 spin = 0; spin < timeout_spins; ++spin) {
                    const bool observed = (read32(0x1c) & controller_ready) != 0;
                    if (observed == ready)
                        return (read32(0x1c) & (1u << 1)) == 0;
                    ASTIA_CPU_RELAX();
                }
                return false;
            }

            bool allocate_queues() {
                return admin_sq_.allocate(1) && admin_cq_.allocate(1) &&
                       io_sq_.allocate(1) && io_cq_.allocate(1) &&
                       identify_page_.allocate(1);
            }

            void set_address(u32 offset, u64 address) {
                write32(offset, static_cast<u32>(address));
                write32(offset + 4, static_cast<u32>(address >> 32));
            }

            bool reset_and_enable() {
                doorbell_stride_ = 4u << static_cast<u32>((cap_ >> 32) & 0xfu);
                write32(0x14, 0);
                if (!wait_ready(false))
                    return false;
                write32(0x24, (queue_depth - 1) << 16 | (queue_depth - 1));
                set_address(0x28, admin_sq_.physical);
                set_address(0x30, admin_cq_.physical);
                write32(0x14, (4u << 20) | (6u << 16) | 1u);
                return wait_ready(true);
            }

            void submit_admin(command& value, u64 data) {
                value.dword[6] = static_cast<u32>(data);
                value.dword[7] = static_cast<u32>(data >> 32);
                auto* entries = static_cast<command*>(admin_sq_.virtual_address);
                entries[admin_sq_tail_] = value;
                admin_sq_tail_ = static_cast<u16>((admin_sq_tail_ + 1) % queue_depth);
                doorbell(0, false, admin_sq_tail_);
            }

            u32 poll_admin() {
                auto* entries = static_cast<volatile completion*>(admin_cq_.virtual_address);
                const u64 hz = arch::timer_hz();
                const u64 deadline = hz == 0 ? 0 : arch::timer_ticks() + hz;
                for (u32 spin = 0; spin < timeout_spins; ++spin) {
                    const u32 dw3 = entries[admin_cq_head_].dword[3];
                    if (((dw3 >> 16) & 1u) == admin_cq_phase_) {
                        const u32 status = (dw3 >> 17) & 0x7ffu;
                        admin_cq_head_ = static_cast<u16>((admin_cq_head_ + 1) % queue_depth);
                        if (admin_cq_head_ == 0)
                            admin_cq_phase_ ^= 1;
                        doorbell(0, true, admin_cq_head_);
                        return status;
                    }
                    if ((spin & 0xffu) == 0) {
                        if ((read32(0x1c) & (1u << 1)) != 0) {
                            return ~u32{0};
                        }
                        if (deadline != 0 && arch::timer_ticks() >= deadline)
                            return ~u32{0};
                        proc::scheduler::yield();
                    }
                    ASTIA_CPU_RELAX();
                }
                return ~u32{0};
            }

            bool identify() {
                command controller_identify{};
                const u16 controller_cid = admin_cid_++;
                controller_identify.dword[0] = admin_identify |
                                               (static_cast<u32>(controller_cid) << 16);
                controller_identify.dword[10] = 1;
                submit_admin(controller_identify, identify_page_.physical);
                const u32 controller_status = poll_admin();
                if (controller_status != 0)
                    return false;
                const auto* data = static_cast<const u8*>(identify_page_.virtual_address);
                const u32 namespaces = core::le<u32>::load(data + 516);
                if (namespaces == 0)
                    return false;
                fill_zero(identify_page_.virtual_address, mm::page_size);
                command namespace_identify{};
                const u16 namespace_cid = admin_cid_++;
                namespace_identify.dword[0] = admin_identify |
                                              (static_cast<u32>(namespace_cid) << 16);
                namespace_identify.dword[1] = namespace_id_;
                submit_admin(namespace_identify, identify_page_.physical);
                if (poll_admin() != 0)
                    return false;
                const u64 size = core::le<u64>::load(data);
                const u8 format = data[26] & 0xfu;
                if (size == 0 || format >= 16)
                    return false;
                const u8 exponent = data[128 + static_cast<usize>(format) * 4 + 2];
                if (exponent < 9 || exponent > 12)
                    return false;
                namespace_size_ = size;
                block_size_ = 1u << exponent;
                return true;
            }

            bool create_io_queues() {
                command cq{};
                const u16 cq_cid = admin_cid_++;
                cq.dword[0] = admin_create_io_cq | (static_cast<u32>(cq_cid) << 16);
                cq.dword[10] = 1 | ((queue_depth - 1) << 16);
                cq.dword[11] = 1;
                submit_admin(cq, io_cq_.physical);
                if (poll_admin() != 0)
                    return false;
                command sq{};
                const u16 sq_cid = admin_cid_++;
                sq.dword[0] = admin_create_io_sq | (static_cast<u32>(sq_cid) << 16);
                sq.dword[10] = 1 | ((queue_depth - 1) << 16);
                sq.dword[11] = 1 | (1u << 16);
                submit_admin(sq, io_sq_.physical);
                return poll_admin() == 0;
            }

            void submit_io(command& value) {
                auto* entries = static_cast<command*>(io_sq_.virtual_address);
                entries[io_sq_tail_] = value;
                io_sq_tail_ = static_cast<u16>((io_sq_tail_ + 1) % queue_depth);
                doorbell(1, false, io_sq_tail_);
            }

            u32 poll_io() {
                auto* entries = static_cast<volatile completion*>(io_cq_.virtual_address);
                const u64 hz = arch::timer_hz();
                const u64 deadline = hz == 0 ? 0 : arch::timer_ticks() + hz;
                for (u32 spin = 0; spin < timeout_spins; ++spin) {
                    const u32 dw3 = entries[io_cq_head_].dword[3];
                    if (((dw3 >> 16) & 1u) == io_cq_phase_) {
                        const u32 status = (dw3 >> 17) & 0x7ffu;
                        io_cq_head_ = static_cast<u16>((io_cq_head_ + 1) % queue_depth);
                        if (io_cq_head_ == 0)
                            io_cq_phase_ ^= 1;
                        doorbell(1, true, io_cq_head_);
                        return status;
                    }
                    if ((spin & 0xffu) == 0) {
                        if ((read32(0x1c) & (1u << 1)) != 0) {
                            return ~u32{0};
                        }
                        if (deadline != 0 && arch::timer_ticks() >= deadline)
                            return ~u32{0};
                        proc::scheduler::yield();
                    }
                    ASTIA_CPU_RELAX();
                }
                return ~u32{0};
            }
        };

        class driver final : public astia::dev::driver {
            public:
            const char* name() const override {
                return "nvme";
            }

            bool match(const device_info& info) const override {
                const pci_device_info* pci = pci_device_info::get(info);
                return pci != nullptr && pci->device().class_code == 0x01 &&
                       pci->device().subclass == 0x08 &&
                       pci->device().programming_interface == 0x02;
            }

            mm::shptr<device> probe(const device_info& info) override {
                const pci_device_info* pci = pci_device_info::get(info);
                if (pci == nullptr)
                    return {};
                auto controller_value = mm::share<controller>();
                if (!controller_value || !controller_value->start(pci->mutable_device()))
                    return {};
                mm::shptr<device> owner = controller_value;
                auto file = mm::share<device_file>(owner);
                if (!file || fs::register_device("/dev/nvme0n1", file) < 0)
                    return {};
                return owner;
            }
        };

        alignas(driver) u8 nvme_driver_storage[sizeof(driver)]{};
        driver* nvme_driver = nullptr;

    } // namespace

    bool init() {
        if (nvme_driver == nullptr)
            nvme_driver = ::new (nvme_driver_storage) driver();
        return drivers().register_driver(*nvme_driver);
    }

    bool test_read() {
        for (u32 i = 0; i < drivers().device_count(); ++i) {
            auto value = drivers().device_at(i);
            auto* found = static_cast<controller*>(value.get());
            const bool interrupts = arch::cpu::check_interrupts();
            if (!interrupts)
                arch::cpu::interrupts();
            if (found == nullptr) {
                if (!interrupts)
                    arch::cpu::nointerrupts();
                return true;
            }
            if (!found->initialize_namespace()) {
                if (!interrupts)
                    arch::cpu::nointerrupts();
                return false;
            }
            {
                auto file = fs::open("/dev/nvme0n1");
                if (!file || file->size() != static_cast<long>(found->size_bytes())) {
                    if (!interrupts)
                        arch::cpu::nointerrupts();
                    return false;
                }
                char sector[512]{};
                if (file->read(sector, sizeof sector, 0) != sizeof sector)
                {
                    if (!interrupts)
                        arch::cpu::nointerrupts();
                    return false;
                }
                const bool valid = sector[0] == 'A' && sector[1] == 'S' && sector[2] == 'T' &&
                       sector[3] == 'I' && sector[4] == 'A' && sector[5] == '_' &&
                       sector[6] == 'N' && sector[7] == 'V';
                if (!interrupts)
                    arch::cpu::nointerrupts();
                if (valid)
                    logs::info("known sector ok");
                return valid;
            }
        }
        return true;
    }

} // namespace astia::dev::nvme
