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

    /// Common socket identity.  Both concrete bases below derive from
    /// `file`, so an `open_file` can hand out a socket through the stable
    /// `as_dgram_sock()` / `as_stream_sock()` capability without RTTI.
    class socket : public file {
        public:
        virtual u16 family() const      = 0;
        virtual u32 socket_type() const = 0;

        virtual long getsockopt(u32 level, u32 name, u32* value) = 0;
    };

    /// Datagram contract: message boundaries are preserved, bind/connect are
    /// address operations and send/recv carry one datagram per call.
    class dgram_sock : public socket {
        public:
        optional<dgram_sock&> as_dgram_sock() override {
            return *this;
        }

        virtual long bind(const astia_sockaddr& address)                = 0;
        virtual long connect(const astia_sockaddr& address)             = 0;
        virtual long set_destination(const astia_sockaddr* destination) = 0;
        virtual long prepare_receive()                                  = 0;
        virtual bool take_source(astia_sockaddr& source)                = 0;

        virtual long set_send_control(const mm::shptr<unix_control>& control) = 0;
        virtual long sendmsg(io::request& request)                            = 0;
        virtual long recvmsg_prepare()                                        = 0;
        virtual long recvmsg_complete(proc::process* receiver, u32 flags,
                                      span<u8> destination, u32& used,
                                      u32& message_flags)                     = 0;
    };

    /// Stream contract: connect/listen/accept plus a byte stream, half-close
    /// and the inherited stream control-message path.
    class stream_sock : public socket {
        public:
        optional<stream_sock&> as_stream_sock() override {
            return *this;
        }

        virtual long bind(const astia_sockaddr& address)    = 0;
        virtual long listen(u32 backlog)                    = 0;
        virtual long accept(io::request& request)           = 0;
        virtual bool accept_address(astia_sockaddr& peer)   = 0;
        virtual long connect(const astia_sockaddr& address) = 0;
        virtual long shutdown(u32 how)                      = 0;

        virtual long set_send_control(const mm::shptr<unix_control>& control) = 0;
        virtual long sendmsg(io::request& request)                            = 0;
        virtual long recvmsg_prepare()                                        = 0;
        virtual long recvmsg_complete(proc::process* receiver, u32 flags,
                                      span<u8> destination, u32& used,
                                      u32& message_flags)                     = 0;
    };
} // namespace astia::fs
