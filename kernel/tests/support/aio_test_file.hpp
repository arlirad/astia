#pragma once

#include <astia/api/errno.h>
#include <astia/fs.hpp>
#include <astia/io.hpp>
#include <astia/mm/share.hpp>

namespace aio_test {

    using namespace astia;

    class fake_file final : public fs::file {
        public:
        explicit fake_file(fs::file_type type = fs::file_type::regular) : type_(type) {}

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

        long truncate(u64 length) override {
            if (truncate_result < 0)
                return truncate_result;
            size_value = static_cast<long>(length);
            return truncate_result;
        }

        long sync() override {
            return sync_result;
        }

        long ioctl(u64, void*, usize) override {
            return ioctl_result;
        }

        long chmod(fs::file_permissions) override {
            return 0;
        }

        long read(void* buffer, u32 length, u64) override {
            if (read_result < 0)
                return read_result;
            return static_cast<long>(length);
        }

        long write(const void*, u32 length, u64) override {
            if (write_result < 0)
                return write_result;
            return static_cast<long>(length);
        }

        fs::file_type type_           = fs::file_type::regular;
        long          submit_result   = 0;
        long          read_result     = 0;
        long          write_result    = 0;
        long          size_value      = 0;
        long          truncate_result = 0;
        long          sync_result     = 0;
        long          ioctl_result    = -ENOTTY;
        u32           poll_events     = 0;
    };

    inline mm::shptr<fs::open_file> make_open_file(fake_file* file, u32 flags = 0) {
        return mm::share<fs::open_file>(mm::shptr<fs::file>(file), flags);
    }

} // namespace aio_test
