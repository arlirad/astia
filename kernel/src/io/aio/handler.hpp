#pragma once

#include <astia/api/aio.h>
#include <astia/common.hpp>
#include <astia/core/cancellation.hpp>
#include <astia/core/task.hpp>
#include <astia/mapping.hpp>

#include <type_traits>

namespace astia::io::aio {
    namespace ring {
        struct ring_slot;
    }

    enum class opcode : u32 {
        nop            = OP_NOP,
        read           = OP_READ,
        write          = OP_WRITE,
        readv          = OP_READV,
        writev         = OP_WRITEV,
        openat         = OP_OPENAT,
        close          = OP_CLOSE,
        statx          = OP_STATX,
        fsync          = OP_FSYNC,
        unlinkat       = OP_UNLINKAT,
        mkdirat        = OP_MKDIRAT,
        renameat       = OP_RENAMEAT,
        sleep          = OP_SLEEP,
        wait           = OP_WAIT,
        poll_add       = OP_POLL_ADD,
        cancel         = OP_CANCEL,
        accept         = OP_ACCEPT,
        connect        = OP_CONNECT,
        send           = OP_SEND,
        recv           = OP_RECV,
        dup            = OP_DUP,
        dup2           = OP_DUP2,
        readdir        = OP_READDIR,
        ioctl          = OP_IOCTL,
        pipe           = OP_PIPE,
        chmod          = OP_CHMOD,
        utimensat      = OP_UTIMENSAT,
        symlinkat      = OP_SYMLINKAT,
        readlinkat     = OP_READLINKAT,
        chown          = OP_CHOWN,
        socket         = OP_SOCKET,
        bind           = OP_BIND,
        listen         = OP_LISTEN,
        socketpair     = OP_SOCKETPAIR,
        shutdown       = OP_SHUTDOWN,
        fcntl          = OP_FCNTL,
        sendto         = OP_SENDTO,
        recvfrom       = OP_RECVFROM,
        eventfd_create = OP_EVENTFD_CREATE,
        linkat         = OP_LINKAT,
        ftruncate      = OP_FTRUNCATE,
        getsockopt     = OP_GETSOCKOPT,
        sendmsg        = OP_SENDMSG,
        recvmsg        = OP_RECVMSG,
        count          = OP_RECVMSG + 1,
    };

    inline constexpr u32 opcode_count = static_cast<u32>(opcode::count);

    constexpr bool valid_opcode(opcode value) {
        return static_cast<u32>(value) < opcode_count;
    }

    /// What one operation body reports to the dispatcher.  A plain integer
    /// (negative errno or a non-negative result) completes the SQE now; an
    /// `error_t` converts to its negative errno.  `retry` leaves the SQE in
    /// the ring and `pending` consumes it with the completion still to come.
    class aio_result {
        public:
        enum class state : u8 {
            retry,
            pending,
            complete,
        };

        aio_result() = default;

        template <typename T>
            requires(std::is_integral_v<T> && !std::is_same_v<T, bool>)
        aio_result(T value) noexcept
            : _state(state::complete), _value(static_cast<i32>(value)) {}

        template <typename T>
            requires(std::is_enum_v<T> && !std::is_same_v<T, error_t>)
        aio_result(T value) noexcept
            : _state(state::complete), _value(static_cast<i32>(value)) {}

        aio_result(error_t error) noexcept
            : _state(state::complete), _value(static_cast<i32>(-errno_value(error))) {}

        aio_result(bool) = delete;

        static const aio_result retry;
        static const aio_result pending;

        state kind() const noexcept {
            return _state;
        }

        i32 value() const noexcept {
            return _value;
        }

        private:
        constexpr explicit aio_result(state value) noexcept : _state(value), _value(0) {}

        state _state = state::retry;
        i32   _value = 0;
    };

    inline const aio_result aio_result::retry{aio_result::state::retry};
    inline const aio_result aio_result::pending{aio_result::state::pending};

    struct operation_descriptor {
        opcode operation;
        core::task<aio_result> (*invoke)(ring::ring_slot&, astia_rings*, astia_sqe,
                                         core::cltoken) noexcept;
    };

    template <typename Derived, opcode Opcode>
    struct handler {
        static constexpr opcode opcode_value = Opcode;

        static core::task<aio_result> thunk(ring::ring_slot& slot, astia_rings* ring,
                                            astia_sqe sqe, core::cltoken token) noexcept {
            static_assert(Derived::opcode_value == Opcode);
            static_assert(valid_opcode(Opcode));

            return Derived::invoke(slot, ring, sqe, token);
        }

        static constexpr operation_descriptor descriptor{Opcode, &handler::thunk};
    };

    constexpr bool validate_manifest(const operation_descriptor* entries, usz count) {
        if (entries == nullptr || count != opcode_count)
            return false;

        bool seen[opcode_count]{};

        for (usz i = 0; i < count; ++i) {
            const operation_descriptor& entry = entries[i];

            const u32 value = static_cast<u32>(entry.operation);

            if (!valid_opcode(entry.operation) || entry.invoke == nullptr || seen[value])
                return false;

            seen[value] = true;
        }

        for (bool present : seen) {
            if (!present)
                return false;
        }

        return true;
    }

    bool validate_descriptors(const operation_descriptor* entries, usz count);

    template <usz N>
    consteval bool valid_descriptors(const operation_descriptor (&entries)[N]) {
        return validate_manifest(entries, N);
    }
} // namespace astia::io::aio
