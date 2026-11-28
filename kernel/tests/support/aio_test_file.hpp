#pragma once

#include <astia/api/errno.h>
#include <astia/fs.hpp>
#include <astia/io.hpp>
#include <astia/mm/share.hpp>

namespace aio_test {
    using namespace astia;

    class fake_file final : public fs::file,
                            public fs::readable,
                            public fs::writable,
                            public fs::pollable,
                            public fs::ioctlable,
                            public fs::truncatable,
                            public fs::syncable {
        public:
        explicit fake_file(fs::file_type type = fs::file_type::regular) : type_(type) {}

        optional<fs::readable&> as_readable() override {
            return *this;
        }

        optional<fs::writable&> as_writable() override {
            return *this;
        }

        optional<fs::pollable&> as_pollable() override {
            return *this;
        }

        optional<fs::ioctlable&> as_ioctlable() override {
            return *this;
        }

        optional<fs::truncatable&> as_truncatable() override {
            return *this;
        }

        optional<fs::syncable&> as_syncable() override {
            return *this;
        }

        core::task<i64> readv(span<const fs::buffer> buffers, u64 offset,
                              core::cltoken token) override {
            return submit_readv(buffers, offset, token);
        }

        core::task<i64> writev(span<const fs::buffer> buffers, u64 offset,
                               core::cltoken token, bool nonblock) override {
            return submit_writev(buffers, offset, token, nonblock);
        }

        long submit(::astia::io::request& request) override {
            if (submit_result < 0)
                return submit_result;
            if (submit_result == 0)
                return 0;
            const u32 moved = static_cast<u32>(submit_result) < request.length()
                                  ? static_cast<u32>(submit_result)
                                  : request.length();
            if (request.direction() == ::astia::io::direction::input &&
                request.buffer() != nullptr)
                __builtin_memset(request.buffer(), 0x5a, moved);
            return moved;
        }

        u32 poll(u32 events, u32) override {
            return poll_events & events;
        }

        fs::file_mode mode() const override {
            return fs::file_mode(type_, fs::default_file_permissions);
        }

        long size() const override {
            return size_value;
        }

        core::task<i64> truncate(u64 length, core::cltoken) override {
            if (truncate_result >= 0)
                size_value = static_cast<long>(length);
            co_return truncate_result;
        }

        core::task<i64> sync(core::cltoken) override {
            co_return sync_result;
        }

        core::task<i64> ioctl(u64, void*, usz, core::cltoken) override {
            co_return ioctl_result;
        }

        long chmod(fs::file_permissions permissions) override {
            chmod_called      = true;
            chmod_permissions = permissions;

            return 0;
        }

        uid_t uid() const override {
            return uid_value;
        }

        gid_t gid() const override {
            return gid_value;
        }

        long chown(uid_t owner, gid_t group) override {
            chown_called = true;
            chown_uid    = owner;
            chown_gid    = group;

            return 0;
        }

        fs::file_timestamps timestamps() const override {
            return recorded_timestamps;
        }

        fs::file_timestamps  recorded_timestamps{};
        fs::file_type        type_           = fs::file_type::regular;
        long                 submit_result   = 0;
        long                 size_value      = 0;
        long                 truncate_result = 0;
        long                 sync_result     = 0;
        long                 ioctl_result    = -ENOTTY;
        u32                  poll_events     = 0;
        uid_t                uid_value{};
        gid_t                gid_value{};
        bool                 chown_called = false;
        uid_t                chown_uid{};
        gid_t                chown_gid{};
        bool                 chmod_called = false;
        fs::file_permissions chmod_permissions{};
    };

    inline mm::shptr<fs::open_file> make_open_file(fake_file* file, u32 flags = 0) {
        return mm::share<fs::open_file>(mm::shptr<fs::file>(file), flags);
    }
} // namespace aio_test
