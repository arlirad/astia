#include "pty.hpp"

#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/spinlock_guard.hpp>

#include "../../../sys/aio.hpp"

namespace astia::sys {
    void io_device_ready_context(const io_device*) __attribute__((weak));

    void io_device_ready_context(const io_device*) {}
}

namespace astia::dev {
    pty::pty(u32 index) : tty("pts", dv_char | dv_sync), _index(index) {}

    bool pty::open_slave() {
        arch::interrupt_spinlock_guard guard(_pty_lock);

        if (!_master_open || _locked)
            return false;

        ++_slave_refs;

        return true;
    }

    void pty::bind_master(fs::file* master) {
        arch::interrupt_spinlock_guard guard(_pty_lock);

        _master_file = master;
    }

    void pty::bind_slave(fs::file* slave) {
        arch::interrupt_spinlock_guard guard(_pty_lock);

        _slave_file = slave;
    }

    bool pty::master_open() const {
        arch::interrupt_spinlock_guard guard(_pty_lock);

        return _master_open;
    }

    bool pty::slave_open() const {
        arch::interrupt_spinlock_guard guard(_pty_lock);

        return _slave_refs != 0;
    }

    void pty::master_closed() {
        fs::file* slave = nullptr;

        {
            arch::interrupt_spinlock_guard guard(_pty_lock);

            if (_master_open) {
                _master_open = false;
                slave        = _slave_file;
                _master_file = nullptr;
            }
        }

        if (slave != nullptr)
            sys::io_device_ready_context(slave);

        release_if_unused();
    }

    void pty::master_duplicate() {
        arch::interrupt_spinlock_guard guard(_pty_lock);

        ++_master_refs;
    }

    void pty::master_close() {
        bool close = false;

        {
            arch::interrupt_spinlock_guard guard(_pty_lock);

            if (_master_refs != 0 && --_master_refs == 0)
                close = true;
        }

        if (close)
            master_closed();
    }

    void pty::duplicate(u32) {
        arch::interrupt_spinlock_guard guard(_pty_lock);

        ++_slave_refs;
    }

    void pty::close(u32) {
        bool notify = false;

        fs::file* master = nullptr;

        {
            arch::interrupt_spinlock_guard guard(_pty_lock);

            if (_slave_refs != 0 && --_slave_refs == 0) {
                notify = true;
                master = _master_open ? _master_file : nullptr;
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
            arch::interrupt_spinlock_guard guard(_pty_lock);

            release = !_master_open && _slave_refs == 0;
        }

        if (release)
            pty_release_slot(_index, this);
    }

    bool pty::master_queue_empty() const {
        arch::interrupt_spinlock_guard guard(_pty_lock);

        return _master_head == _master_tail;
    }

    long pty::master_read(void* buffer, u32 length) {
        if (buffer == nullptr && length != 0)
            return -EFAULT;

        u32 count = 0;

        fs::file* slave = nullptr;

        {
            arch::interrupt_spinlock_guard guard(_pty_lock);

            const u32 available = _master_head - _master_tail;

            count = available < length ? available : length;

            auto* out = static_cast<u8*>(buffer);

            for (u32 i = 0; i < count; ++i) {
                out[i] = _master_queue[(_master_tail + i) & (queue_capacity - 1)];
            }

            _master_tail += count;
            slave = _slave_file;
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
            const tty_input_result result = tty_receive(input[count]);

            if (result == tty_input_result::blocked)
                break;

            if (result == tty_input_result::ready)
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
            arch::interrupt_spinlock_guard guard(_pty_lock);

            if (_master_head != _master_tail)
                result |= POLLIN;

            if (!_master_open || _slave_refs == 0)
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
                *static_cast<i32*>(argp) = static_cast<i32>(_index);

                return 0;
            }

            arch::interrupt_spinlock_guard guard(_pty_lock);

            if (request == TIOCSPTLCK) {
                const i32 value = *static_cast<const i32*>(argp);

                if (value != 0 && value != 1)
                    return -EINVAL;

                _locked = value != 0;

                return 0;
            }

            *static_cast<i32*>(argp) = _locked ? 1 : 0;

            return 0;
        }

        return -ENOTTY;
    }

    long pty::read(void* buffer, u32 length, u64 offset) {
        if (!master_open()) {
            const long result = tty::read(buffer, length, offset);

            return result == 0 ? -EIO : result;
        }

        const long result = tty::read(buffer, length, offset);
        if (result > 0) {
            fs::file* master = nullptr;

            {
                arch::interrupt_spinlock_guard guard(_pty_lock);

                master = _master_file;
            }

            if (master != nullptr)
                sys::io_device_ready_context(master);
        }

        return result;
    }

    long pty::write(const void* buffer, u32 length, u64 offset) {
        return master_open() ? tty::write(buffer, length, offset) : -EIO;
    }

    long pty::submit(io::request& request) {
        if (request.direction() == io::direction::output && !master_open())
            return -EIO;

        if (request.direction() == io::direction::input && !master_open()) {
            const long result =
                tty::read(request.buffer(), request.length(), request.offset());

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

            arch::interrupt_spinlock_guard guard(_pty_lock);

            *static_cast<struct winsize*>(argp) = _winsize;

            return 0;
        }

        if (request == TIOCSWINSZ) {
            if (argp == nullptr || size != sizeof(struct winsize))
                return -EINVAL;

            arch::interrupt_spinlock_guard guard(_pty_lock);

            _winsize = *static_cast<const struct winsize*>(argp);

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
            arch::interrupt_spinlock_guard guard(_pty_lock);

            if (!_master_open)
                return false;

            if (_master_head - _master_tail == queue_capacity)
                return false;

            _master_queue[_master_head & (queue_capacity - 1)] = byte;

            ++_master_head;

            inserted = true;
            master   = _master_file;
        }

        if (inserted && master != nullptr)
            sys::io_device_ready_context(master);

        return inserted;
    }

    void pty::tty_ready() {
        fs::file* slave;

        {
            arch::interrupt_spinlock_guard guard(_pty_lock);

            slave = _slave_file;
        }

        if (slave != nullptr)
            sys::io_device_ready_context(slave);
    }

    long pty::tty_speed(speed_t) {
        return 0;
    }

    long pty::tty_discard_output() {
        arch::interrupt_spinlock_guard guard(_pty_lock);

        _master_tail = _master_head;

        return 0;
    }
} // namespace astia::dev
