#include "device_file.hpp"
#include <astia/dev/blk/blkdev.hpp>

#include <astia/api/errno.h>

namespace astia::dev {

    void device_file::detach() {
        __atomic_store_n(&detached_, true, __ATOMIC_RELEASE);
        while (__atomic_load_n(&active_, __ATOMIC_ACQUIRE) != 0)
            ASTIA_CPU_RELAX();
    }

    device* device_file::begin_operation() const {
        __atomic_add_fetch(&active_, 1u, __ATOMIC_ACQ_REL);
        if (__atomic_load_n(&detached_, __ATOMIC_ACQUIRE)) {
            __atomic_sub_fetch(&active_, 1u, __ATOMIC_ACQ_REL);
            return nullptr;
        }
        device* value = device_instance();
        if (value == nullptr)
            __atomic_sub_fetch(&active_, 1u, __ATOMIC_ACQ_REL);
        return value;
    }

    void device_file::end_operation() const {
        __atomic_sub_fetch(&active_, 1u, __ATOMIC_ACQ_REL);
    }

    long device_file::read(void* buf, u32 len, u64 offset) {
        device* value = begin_operation();
        if (value == nullptr)
            return -ENODEV;
        const long result = value->read(astia::span<u8>(static_cast<u8*>(buf), len), offset);
        end_operation();
        return result;
    }

    long device_file::write(const void* buf, u32 len, u64 offset) {
        device* value = begin_operation();
        if (value == nullptr)
            return -ENODEV;
        const long result =
            value->write(astia::span<const u8>(static_cast<const u8*>(buf), len), offset);
        end_operation();
        return result;
    }

    long device_file::submit(io::request& request) {
        device* value = begin_operation();
        if (value == nullptr)
            return -ENODEV;
        const long result = value->submit(request);
        end_operation();
        return result;
    }
    u32 device_file::poll(u32 events, u32 flags) {
        device* value = begin_operation();
        if (value == nullptr)
            return static_cast<u32>(POLLNVAL);
        const u32 result = value->poll(events, flags);
        end_operation();
        return result;
    }

    long device_file::ioctl(u64 request, void* argp, usize size) {
        device* value = begin_operation();
        if (value == nullptr)
            return -ENODEV;
        const long result = value->ioctl(request, astia::span<u8>(static_cast<u8*>(argp), size));
        end_operation();
        return result;
    }

    long device_file::acquire_controlling_terminal(i32 group) {
        device* value = begin_operation();
        if (value == nullptr)
            return -ENODEV;
        const long result = value->acquire_controlling_terminal(group);
        end_operation();
        return result;
    }

    long device_file::size() const {
        device* value = begin_operation();
        if (value == nullptr)
            return -ENODEV;
        const long result = (value->flags() & dv_block) != 0
                                ? static_cast<long>(static_cast<const blk::blkdev*>(value)
                                                        ->size_bytes())
                                : -ESPIPE;
        end_operation();
        return result;
    }

    u32 device_file::block_size() const {
        device* value = begin_operation();
        if (value == nullptr)
            return 0;
        const u32 result = (value->flags() & dv_block) != 0
                               ? static_cast<const blk::blkdev*>(value)->block_size()
                               : 0;
        end_operation();
        return result;
    }

} // namespace astia::dev
