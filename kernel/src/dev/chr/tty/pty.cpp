#include "pty.hpp"

#include "../../../sys/aio.hpp"
#include <astia/arch/cpu.hpp>
#include <astia/api/errno.h>

namespace astia::sys {
    void io_device_ready_context(const io_device*) __attribute__((weak));
    void io_device_ready_context(const io_device*) {}
}

namespace astia::dev {
    pty::pty(u32 index) : tty("pts", dv_char | dv_sync), index_(index) {}

    bool pty::open_slave() {
        arch::interrupt_spinlock_guard guard(pty_lock_);
        if (!master_open_ || locked_)
            return false;
        ++slave_refs_;
        return true;
    }

    void pty::bind_master(fs::file* master) {
        arch::interrupt_spinlock_guard guard(pty_lock_);
        master_file_ = master;
    }

    void pty::bind_slave(fs::file* slave) {
        arch::interrupt_spinlock_guard guard(pty_lock_);
        slave_file_ = slave;
    }

    bool pty::master_open() const {
        arch::interrupt_spinlock_guard guard(pty_lock_);
        return master_open_;
    }

    bool pty::slave_open() const {
        arch::interrupt_spinlock_guard guard(pty_lock_);
        return slave_refs_ != 0;
    }

    void pty::master_closed() {
        fs::file* slave = nullptr;
        {
            arch::interrupt_spinlock_guard guard(pty_lock_);
            if (master_open_) {
                master_open_ = false;
                slave = slave_file_;
                master_file_ = nullptr;
            }
        }
        if (slave != nullptr)
            sys::io_device_ready_context(slave);
        release_if_unused();
    }

    void pty::master_duplicate() {
        arch::interrupt_spinlock_guard guard(pty_lock_);
        ++master_refs_;
    }

    void pty::master_close() {
        bool close = false;
        {
            arch::interrupt_spinlock_guard guard(pty_lock_);
            if (master_refs_ != 0 && --master_refs_ == 0)
                close = true;
        }
        if (close)
            master_closed();
    }

    void pty::duplicate(u32) {
        arch::interrupt_spinlock_guard guard(pty_lock_);
        ++slave_refs_;
    }

    void pty::close(u32) {
        bool notify = false;
        fs::file* master = nullptr;
        {
            arch::interrupt_spinlock_guard guard(pty_lock_);
            if (slave_refs_ != 0 && --slave_refs_ == 0) {
                notify = true;
                master = master_open_ ? master_file_ : nullptr;
            }
        }
        if (notify) {
            if (master != nullptr)
                sys::io_device_ready_context(master);
            release_if_unused();
        }
    }

    void pty::release_if_unused() {
        bool release;
        {
            arch::interrupt_spinlock_guard guard(pty_lock_);
            release = !master_open_ && slave_refs_ == 0;
        }
        if (release)
            pty_release_slot(index_, this);
    }

    bool pty::master_queue_empty() const {
        arch::interrupt_spinlock_guard guard(pty_lock_);
        return master_head_ == master_tail_;
    }

    long pty::master_read(void* buffer, u32 length) {
        if (buffer == nullptr && length != 0)
            return -EFAULT;
        u32 count = 0;
        fs::file* slave = nullptr;
        {
            arch::interrupt_spinlock_guard guard(pty_lock_);
            const u32 available = master_head_ - master_tail_;
            count = available < length ? available : length;
            auto* out = static_cast<u8*>(buffer);
            for (u32 i = 0; i < count; ++i)
                out[i] = master_queue_[(master_tail_ + i) & (queue_capacity - 1)];
            master_tail_ += count;
            slave = slave_file_;
        }
        if (count != 0)
            if (slave != nullptr)
                sys::io_device_ready_context(slave);
        if (count == 0 && !slave_open())
            return -EIO;
        return static_cast<long>(count);
    }

    long pty::master_write(const void* buffer, u32 length) {
        if (buffer == nullptr && length != 0)
            return -EFAULT;
        if (!master_open() || !slave_open())
            return -EIO;
        const auto* input = static_cast<const u8*>(buffer);
        u32 count = 0;
        for (; count < length; ++count) {
            const bool ready = tty_receive(input[count]);
            if (ready)
                tty_input_ready();
        }
        return static_cast<long>(count);
    }

    long pty::master_submit(io::request& request) {
        const long result = request.direction() == io::direction::input
                                ? master_read(request.buffer(), request.length())
                                : master_write(request.buffer(), request.length());
        if (result != 0 || request.length() == 0)
            return result;
        if (request.direction() == io::direction::input && !slave_open())
            return -EIO;
        request.stage(1);
        return 0;
    }

    u32 pty::master_poll(u32 events) const {
        u32 result = 0;
        {
            arch::interrupt_spinlock_guard guard(pty_lock_);
            if (master_head_ != master_tail_)
                result |= POLLIN;
            if (!master_open_ || slave_refs_ == 0)
                result |= POLLHUP;
            else
                result |= POLLOUT;
        }
        return result & (events | POLLERR | POLLHUP);
    }

    long pty::master_ioctl(u64 request, void* argp, usize size) {
        if (request == TIOCGPTN || request == TIOCSPTLCK || request == TIOCGPTLCK) {
            if (argp == nullptr || size != sizeof(i32))
                return -EINVAL;
            if (request == TIOCGPTN) {
                *static_cast<i32*>(argp) = static_cast<i32>(index_);
                return 0;
            }
            arch::interrupt_spinlock_guard guard(pty_lock_);
            if (request == TIOCSPTLCK) {
                const i32 value = *static_cast<const i32*>(argp);
                if (value != 0 && value != 1)
                    return -EINVAL;
                locked_ = value != 0;
                return 0;
            }
            *static_cast<i32*>(argp) = locked_ ? 1 : 0;
            return 0;
        }
        return -ENOTTY;
    }

    long pty::read(void* buffer, u32 length, u64 offset) {
        if (!master_open()) {
            const long result = tty::read(buffer, length, offset);
            return result == 0 ? -EIO : result;
        }
        return tty::read(buffer, length, offset);
    }

    long pty::write(const void* buffer, u32 length, u64 offset) {
        return master_open() ? tty::write(buffer, length, offset) : -EIO;
    }

    long pty::submit(io::request& request) {
        if (request.direction() == io::direction::output && !master_open())
            return -EIO;
        if (request.direction() == io::direction::input && !master_open()) {
            const long result = tty::read(request.buffer(), request.length(), request.offset());
            return result == 0 ? -EIO : result;
        }
        return tty::submit(request);
    }

    u32 pty::poll(u32 events, u32 flags) {
        u32 result = tty::poll(events, flags);
        if (!master_open())
            result |= POLLHUP;
        return result & (events | POLLERR | POLLHUP);
    }

    long pty::ioctl(u64 request, void* argp, usize size) {
        if (request == TIOCGWINSZ) {
            if (argp == nullptr || size != sizeof(struct winsize))
                return -EINVAL;
            arch::interrupt_spinlock_guard guard(pty_lock_);
            *static_cast<struct winsize*>(argp) = winsize_;
            return 0;
        }
        if (request == TIOCSWINSZ) {
            if (argp == nullptr || size != sizeof(struct winsize))
                return -EINVAL;
            arch::interrupt_spinlock_guard guard(pty_lock_);
            winsize_ = *static_cast<const struct winsize*>(argp);
            return 0;
        }
        return tty::ioctl(request, argp, size);
    }

    long pty::acquire_controlling_terminal(i32 group) {
        return tty::acquire_controlling_terminal(group);
    }

    bool pty::tty_putc(u8 byte) {
        bool inserted = false;
        fs::file* master = nullptr;
        {
            arch::interrupt_spinlock_guard guard(pty_lock_);
            if (!master_open_)
                return false;
            if (master_head_ - master_tail_ == queue_capacity)
                return false;
            master_queue_[master_head_ & (queue_capacity - 1)] = byte;
            ++master_head_;
            inserted = true;
            master = master_file_;
        }
        if (inserted && master != nullptr)
            sys::io_device_ready_context(master);
        return inserted;
    }

    void pty::tty_ready() {
        fs::file* slave;
        {
            arch::interrupt_spinlock_guard guard(pty_lock_);
            slave = slave_file_;
        }
        if (slave != nullptr)
            sys::io_device_ready_context(slave);
    }

    long pty::tty_speed(speed_t) {
        return 0;
    }

} // namespace astia::dev
