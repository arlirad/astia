#pragma once
#include <astia/api/aio.h>
#include <astia/fs.hpp>
#include <astia/span.hpp>

#include "unix_control.hpp"

namespace astia::proc {
    class process;
}

namespace astia::fs {
    /// Private protocol capability.  The callbacks operate on the original
    /// open file description, preserving descriptor identity and lifetime;
    /// the AIO layer never needs to know the concrete socket implementation.
    /// Pathname operations take the caller-resolved canonical path, so the
    /// socket layer never resolves a raw address against the global root.
    struct unix_socket_operations {
        long (*bind)(open_file&, const astia_sockaddr&, const char* canonical);
        long (*listen)(open_file&, u32);
        long (*accept)(open_file&, io::request&);
        bool (*accept_address)(open_file&, astia_sockaddr&);
        long (*connect)(open_file&, const astia_sockaddr&, const char* canonical);
        long (*shutdown)(open_file&, u32);
        long (*getsockopt)(open_file&, u32, u32, u32*);
        long (*sendto)(open_file&, const char* canonical);
        long (*recvfrom)(open_file&);
        bool (*recvfrom_address)(open_file&, astia_sockaddr&);
        long (*set_send_control)(open_file&, const mm::shptr<unix_control>&);
        long (*sendmsg)(open_file&, io::request&);
        long (*recvmsg_prepare)(open_file&);
        long (*recvmsg_complete)(open_file&, proc::process*, u32, span<u8>, u32&, u32&);
    };

    long create_unix_socket(mm::shptr<open_file>& out, u32 type);
    long unix_socketpair(mm::shptr<open_file>& left, mm::shptr<open_file>& right,
                         u32 type);
} // namespace astia::fs
