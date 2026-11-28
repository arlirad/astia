#pragma once

#include <astia/api/aio.h>
#include <astia/common.hpp>
#include <astia/core/task.hpp>
#include <astia/fs/file.hpp>
#include <astia/mm/shptr.hpp>
#include <astia/span.hpp>

namespace astia::proc {
    class process;
}

namespace astia::io {
    class request;
}

namespace astia::fs {
    struct unix_control;

    /// Result of one receive: data length (negative errno on failure), the
    /// sender/peer address, and any passed control message.
    struct message {
        i64                     length = 0;
        astia_sockaddr          source{};
        mm::shptr<unix_control> control{};
        u32                     flags = 0;
    };

    /// Result of one accept: an installed descriptor or negative errno, plus
    /// the peer address the listener recorded.
    struct accepted {
        i64            fd = -1;
        astia_sockaddr peer{};
    };

    /// Common socket identity.  Both concrete bases below derive from
    /// `file`, so an `open_file` can hand out a socket through the stable
    /// `as_dgram_sock()` / `as_stream_sock()` capability without RTTI.
    class socket : public file {
        public:
        virtual u16 family() const      = 0;
        virtual u32 socket_type() const = 0;

        virtual long getsockopt(u32 level, u32 name, u32* value) = 0;
    };

    /// Datagram contract.  Every data operation is one task: the caller
    /// awaits the method instead of preparing state and submitting later.
    class dgram_sock : public socket {
        public:
        optional<dgram_sock&> as_dgram_sock() override {
            return *this;
        }

        virtual long bind(const astia_sockaddr& address)    = 0;
        virtual long connect(const astia_sockaddr& address) = 0;

        virtual core::task<i64>     sendto(span<const u8>        data,
                                           const astia_sockaddr& destination,
                                           core::cltoken         token = {})                  = 0;
        virtual core::task<message> recvfrom(span<u8> data, core::cltoken token = {}) = 0;

        virtual core::task<i64>     sendmsg(span<const u8>                 data,
                                            const astia_sockaddr*          destination,
                                            const mm::shptr<unix_control>& control,
                                            core::cltoken                  token = {})                = 0;
        virtual core::task<message> recvmsg(span<u8> data, core::cltoken token = {}) = 0;
    };

    /// Stream contract: connect/listen/accept plus a byte stream, half-close
    /// and control messages.
    class stream_sock : public socket {
        public:
        optional<stream_sock&> as_stream_sock() override {
            return *this;
        }

        virtual long                 bind(const astia_sockaddr& address)    = 0;
        virtual long                 listen(u32 backlog)                    = 0;
        virtual core::task<accepted> accept(core::cltoken token = {})       = 0;
        virtual long                 connect(const astia_sockaddr& address) = 0;
        virtual long                 shutdown(u32 how)                      = 0;

        virtual core::task<i64>     sendmsg(span<const u8>                 data,
                                            const mm::shptr<unix_control>& control,
                                            core::cltoken                  token = {})                = 0;
        virtual core::task<message> recvmsg(span<u8> data, core::cltoken token = {}) = 0;
    };
} // namespace astia::fs
