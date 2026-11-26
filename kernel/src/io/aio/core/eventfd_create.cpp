#include <astia/atomic.hpp>

#include "common.hpp"

namespace astia::io::aio::operations {
    namespace {
        class eventfd_file final : public fs::file,
                                   public fs::readable,
                                   public fs::writable,
                                   public fs::pollable {
            public:
            eventfd_file(u64 value, bool nonblock) : _value(value), _nonblock(nonblock) {}

            optional<fs::readable&> as_readable() override {
                return *this;
            }

            optional<fs::writable&> as_writable() override {
                return *this;
            }

            optional<fs::pollable&> as_pollable() override {
                return *this;
            }

            fs::file_mode mode() const override {
                return fs::file_mode(fs::file_type::character,
                                     fs::default_device_permissions);
            }

            core::task<i64> readv(span<const fs::buffer> buffers, u64 offset,
                                  core::cltoken token) override {
                return submit_readv(buffers, offset, token);
            }

            core::task<i64> writev(span<const fs::buffer> buffers, u64 offset,
                                   core::cltoken token) override {
                return submit_writev(buffers, offset, token);
            }

            long read_ready(void* buffer, u32 length, u64) override {
                if (buffer == nullptr || length != sizeof(u64))
                    return -EINVAL;
                u64 value = _value.exchange(0, astia::memory_order::acq_rel);
                if (value == 0)
                    return 0;
                *static_cast<u64*>(buffer) = value;
                io_device_ready_context(this);
                return sizeof(u64);
            }

            long write_ready(const void* buffer, u32 length, u64) override {
                if (buffer == nullptr || length != sizeof(u64))
                    return -EINVAL;
                u64 add = *static_cast<const u64*>(buffer);
                if (add == UINT64_MAX)
                    return -EINVAL;
                u64 old = _value.load(astia::memory_order::relaxed);
                while (true) {
                    if (UINT64_MAX - old < add)
                        return _nonblock ? -EAGAIN : 0;

                    if (_value.compare_exchange_strong(old, old + add,
                                                       astia::memory_order::release,
                                                       astia::memory_order::relaxed)) {
                        io_device_ready_context(this);

                        return sizeof(u64);
                    }
                }
            }

            u32 poll(u32 events, u32) override {
                u32 ready = 0;
                if (_value.load(astia::memory_order::acquire) != 0)
                    ready |= POLLIN;
                if (_value.load(astia::memory_order::acquire) != UINT64_MAX)
                    ready |= POLLOUT;
                return ready & events;
            }

            long submit(::astia::io::request& request) override {
                if (request.length() != sizeof(u64))
                    return -EINVAL;
                const long result =
                    request.direction() == ::astia::io::direction::input
                        ? read_ready(request.buffer(), request.length(), 0)
                        : write_ready(request.buffer(), request.length(), 0);
                if (result == 0 && _nonblock)
                    return -EAGAIN;
                if (result == 0)
                    request.stage(1);
                return result;
            }

            private:
            astia::atomic<u64> _value;
            bool               _nonblock;
        };
    } // namespace

    struct eventfd_create : handler<eventfd_create, opcode::eventfd_create> {
        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               astia_sqe sqe, [[maybe_unused]] core::cltoken token) noexcept {
            if ((sqe.op_flags & ~(ASTIA_EFD_NONBLOCK | ASTIA_EFD_CLOEXEC)) != 0 ||
                sqe.offset == UINT64_MAX)
                co_return -EINVAL;

            proc::process* current = proc::current_process();
            if (!current)
                co_return -ESRCH;

            mm::shptr<eventfd_file> event(
                new eventfd_file(sqe.offset, (sqe.op_flags & ASTIA_EFD_NONBLOCK) != 0));

            if (!event)
                co_return -ENOMEM;

            const u32 status_flags =
                sqe.op_flags & ASTIA_EFD_NONBLOCK
                    ? static_cast<u32>(fs::open_file_status_flags::nonblock)
                    : 0u;
            const u32 descriptor_flags = sqe.op_flags & ASTIA_EFD_CLOEXEC
                                             ? static_cast<u32>(ASTIA_FD_CLOEXEC)
                                             : 0u;

            auto handle = mm::share<fs::open_file>(
                event, static_cast<fs::open_file_status_flags>(status_flags));

            for (i32 fd = 3; fd < (i32) proc::max_process_fds; ++fd) {
                if (!current->descriptor(fd_t{fd}) &&
                    current->set_descriptor(fd_t{fd}, handle, descriptor_flags))
                    co_return fd;
            }

            co_return -EMFILE;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor eventfd_create_descriptor =
        eventfd_create::descriptor;
} // namespace astia::io::aio::operations
