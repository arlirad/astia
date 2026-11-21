#pragma once

#include <astia/api/aio.h>
#include <astia/common.hpp>

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
        spawn          = OP_SPAWN,
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
        count          = OP_GETSOCKOPT + 1,
    };

    inline constexpr u32 opcode_count = static_cast<u32>(opcode::count);

    constexpr bool valid_opcode(opcode value) {
        return static_cast<u32>(value) < opcode_count;
    }

    struct descriptor {
        opcode operation;
        bool (*invoke)(ring::ring_slot&, astia_rings*, const astia_sqe&) noexcept;
    };

    template <typename Derived, opcode Opcode>
    struct handler {
        static constexpr opcode opcode_value = Opcode;

        static bool invoke(ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            static_assert(Derived::opcode_value == Opcode);
            static_assert(valid_opcode(Opcode));
            return Derived::handle(slot, ring, sqe);
        }

        static constexpr descriptor describe() noexcept {
            static_assert(valid_opcode(Opcode));
            return {Opcode, &handler::invoke};
        }
    };

    constexpr bool validate_manifest(const descriptor* entries, usize count) {
        if (entries == nullptr || count != opcode_count)
            return false;
        bool seen[opcode_count]{};
        for (usize i = 0; i < count; ++i) {
            const descriptor& entry = entries[i];
            const u32         value = static_cast<u32>(entry.operation);
            if (!valid_opcode(entry.operation) || entry.invoke == nullptr || seen[value])
                return false;
            seen[value] = true;
        }
        for (bool present : seen)
            if (!present)
                return false;
        return true;
    }

    bool validate_descriptors(const descriptor* entries, usize count);

    template <usize N>
    consteval bool valid_descriptors(const descriptor (&entries)[N]) {
        return validate_manifest(entries, N);
    }

} // namespace astia::io::aio
