#include "unix_control.hpp"

#include <astia/api/errno.h>

#include "../proc/process.hpp"

namespace astia::fs {
    namespace {
        constexpr u32 cmsg_alignment = sizeof(u64);

        constexpr u32 align_cmsg(u32 value) {
            return (value + cmsg_alignment - 1) & ~(cmsg_alignment - 1);
        }
    } // namespace

    long parse_unix_control(proc::process& sender, span<const u8> control,
                            mm::shptr<unix_control>& out) {
        if (control.empty()) {
            out = {};

            return 0;
        }

        if (control.length() > unix_control_max_bytes)
            return -EINVAL;

        auto result = mm::share<unix_control>();
        if (!result)
            return -ENOMEM;

        u32 offset = 0;

        while (offset < control.length()) {
            if (control.length() - offset < sizeof(astia_cmsghdr))
                return -EINVAL;

            astia_cmsghdr header{};

            __builtin_memcpy(&header, control.data() + offset, sizeof header);

            if (header.cmsg_len < sizeof(astia_cmsghdr) ||
                header.cmsg_len > control.length() - offset)
                return -EINVAL;

            if (header.cmsg_level != SOL_SOCKET || header.cmsg_type != ASTIA_SCM_RIGHTS)
                return -EINVAL;

            const u32 payload = static_cast<u32>(header.cmsg_len - sizeof header);

            if (payload == 0 || payload % sizeof(i32) != 0)
                return -EINVAL;

            const u32 count = payload / sizeof(i32);

            if (count > unix_control_max_fds - result->count)
                return -EINVAL;

            for (u32 i = 0; i < count; ++i) {
                i32 raw_fd = 0;

                __builtin_memcpy(
                    &raw_fd, control.data() + offset + sizeof header + i * sizeof(i32),
                    sizeof raw_fd);

                auto file = sender.descriptor(fd_t{raw_fd});
                if (!file)
                    return -EBADF;

                result->files[result->count++] = file;
            }

            const u32 next = offset + align_cmsg(static_cast<u32>(header.cmsg_len));

            if (next > control.length()) {
                if (offset + header.cmsg_len != control.length())
                    return -EINVAL;

                offset = control.length();
            }
            else {
                offset = next;
            }
        }

        out = result;

        return 0;
    }

    long build_unix_control(proc::process& receiver, const unix_control& control,
                            u32 flags, span<u8> destination, u32& used, u32& msg_flags) {
        used      = 0;
        msg_flags = 0;

        if (control.count == 0)
            return 0;

        const u32 payload  = control.count * sizeof(i32);
        const u32 cmsg_len = align_cmsg(sizeof(astia_cmsghdr)) + payload;
        const u32 total    = align_cmsg(cmsg_len);

        if (destination.length() < total) {
            msg_flags |= ASTIA_MSG_CTRUNC;

            return 0;
        }

        i32       installed[unix_control_max_fds]{};
        u32       installed_count = 0;
        const u32 descriptor_flags =
            (flags & ASTIA_MSG_CMSG_CLOEXEC) != 0 ? ASTIA_FD_CLOEXEC : 0;

        for (u32 i = 0; i < control.count; ++i) {
            bool placed = false;

            for (i32 raw = 3; raw < static_cast<i32>(proc::max_process_fds); ++raw) {
                const fd_t fd{raw};

                if (receiver.descriptor(fd))
                    continue;

                if (!receiver.set_descriptor(fd, control.files[i], descriptor_flags))
                    break;

                installed[installed_count++] = raw;
                placed                       = true;

                break;
            }

            if (!placed) {
                for (u32 rollback = 0; rollback < installed_count; ++rollback) {
                    (void) receiver.clear_descriptor(fd_t{installed[rollback]});
                }

                return -EMFILE;
            }
        }

        astia_cmsghdr header{cmsg_len, SOL_SOCKET, ASTIA_SCM_RIGHTS};

        __builtin_memcpy(destination.data(), &header, sizeof header);
        __builtin_memcpy(destination.data() + sizeof header, installed, payload);

        for (u32 i = sizeof header + payload; i < total; ++i) {
            destination[i] = 0;
        }

        used = total;

        return 0;
    }
} // namespace astia::fs
