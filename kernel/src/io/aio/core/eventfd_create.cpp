#include "common.hpp"

namespace astia::io::aio::operations {

#ifndef ASTIA_AIO_HOST_TEST
    namespace {

        class eventfd_file final : public fs::file {
            public:
            eventfd_file(u64 value, bool nonblock) : _value(value), _nonblock(nonblock) {}

            fs::file_mode mode() const override {
                return fs::file_mode(fs::file_type::character,
                                     fs::default_device_permissions);
            }

            long read(void* buffer, u32 length, u64) override {
                if (buffer == nullptr || length != sizeof(u64))
                    return -EINVAL;
                u64 value = __atomic_exchange_n(&_value, 0, __ATOMIC_ACQ_REL);
                if (value == 0)
                    return 0;
                *static_cast<u64*>(buffer) = value;
                io_device_ready_context(this);
                return sizeof(u64);
            }

            long write(const void* buffer, u32 length, u64) override {
                if (buffer == nullptr || length != sizeof(u64))
                    return -EINVAL;
                u64 add = *static_cast<const u64*>(buffer);
                if (add == UINT64_MAX)
                    return -EINVAL;
                u64 old = __atomic_load_n(&_value, __ATOMIC_RELAXED);
                for (;;) {
                    if (UINT64_MAX - old < add)
                        return _nonblock ? -EAGAIN : 0;
                    if (__atomic_compare_exchange_n(&_value, &old, old + add, false,
                                                    __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
                        io_device_ready_context(this);
                        return sizeof(u64);
                    }
                }
            }

            u32 poll(u32 events, u32) override {
                u32 ready = 0;
                if (__atomic_load_n(&_value, __ATOMIC_ACQUIRE) != 0)
                    ready |= POLLIN;
                if (__atomic_load_n(&_value, __ATOMIC_ACQUIRE) != UINT64_MAX)
                    ready |= POLLOUT;
                return ready & events;
            }

            long submit(::astia::io::request& request) override {
                if (request.length() != sizeof(u64))
                    return -EINVAL;
                const long result = request.direction() == ::astia::io::direction::input
                                        ? read(request.buffer(), request.length(), 0)
                                        : write(request.buffer(), request.length(), 0);
                if (result == 0 && _nonblock)
                    return -EAGAIN;
                if (result == 0)
                    request.stage(1);
                return result;
            }

            private:
            u64  _value;
            bool _nonblock;
        };

        bool handle_eventfd_create(astia_rings* ring, const astia_sqe& s) {
            if ((s.op_flags & ~(ASTIA_EFD_NONBLOCK | ASTIA_EFD_CLOEXEC)) != 0 ||
                s.offset == UINT64_MAX)
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            proc::process* current = proc::current_process();
            if (!current)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            mm::shptr<eventfd_file> event(
                new eventfd_file(s.offset, (s.op_flags & ASTIA_EFD_NONBLOCK) != 0));
            if (!event)
                return cq_post_guarded(ring, s.user_data, -ENOMEM);
            const u32 status_flags =
                s.op_flags & ASTIA_EFD_NONBLOCK
                    ? static_cast<u32>(fs::open_file_status_flags::nonblock)
                    : 0u;
            const u32 descriptor_flags =
                s.op_flags & ASTIA_EFD_CLOEXEC ? static_cast<u32>(ASTIA_FD_CLOEXEC) : 0u;
            auto object = mm::share<fs::open_file>(
                event, static_cast<fs::open_file_status_flags>(status_flags));
            for (i32 fd = 3; fd < (i32) proc::max_process_fds; ++fd)
                if (!current->descriptor(fd_t{fd}) &&
                    current->set_descriptor(fd_t{fd}, object, descriptor_flags))
                    return cq_post_guarded(ring, s.user_data, fd);
            return cq_post_guarded(ring, s.user_data, -EMFILE);
        }

    } // namespace
#endif

    struct eventfd_create : handler<eventfd_create, opcode::eventfd_create> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
#ifdef ASTIA_AIO_HOST_TEST
            return ::astia::io::aio::dispatch::dispatch_core(slot, ring, sqe);
#else
            return handle_eventfd_create(ring, sqe);
#endif
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor eventfd_create_descriptor =
        eventfd_create::descriptor;

} // namespace astia::io::aio::operations
