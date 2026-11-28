#pragma once

#include <astia/api/errno.h>
#include <astia/fs/socket.hpp>
#include <astia/mapping.hpp>

#include "../../fs/mapping.hpp"
#include "../../io/aio/dispatch.hpp"
#include "../../io/aio/memory.hpp"
#include "../../io/aio/ring.hpp"
#include "../../ipc/pipe.hpp"
#include "../../ipc/unix_socket.hpp"
#include "../../proc/mapping.hpp"
#include "../../proc/proc.hpp"

namespace astia::io::aio::operations {
    using namespace ::astia::io::aio::ring;
    using ::astia::io::aio::dispatch::file_for;
    using ::astia::io::aio::dispatch::parent_writable;
    using ::astia::io::aio::dispatch::submit_socket_request;

    inline constexpr usz max_operation_bytes = ASTIA_AIO_MAX_OPERATION_BYTES;

    inline fs::dgram_sock* dgram_for(const mm::shptr<fs::open_file>& handle) {
        if (!handle)
            return nullptr;

        auto socket = handle->parent()->as_dgram_sock();

        return socket ? &*socket : nullptr;
    }

    inline fs::stream_sock* stream_for(const mm::shptr<fs::open_file>& handle) {
        if (!handle)
            return nullptr;

        auto socket = handle->parent()->as_stream_sock();

        return socket ? &*socket : nullptr;
    }

    inline bool socket_node_writable(proc::process* current, const char* path) {
        if (!current || !path)
            return false;

        const fs::file_type type = fs::node_type(path);

        if (type == fs::directory_unknown || type != fs::directory_sock)
            return true;

        return current->can_access(fs::node_mode(path), fs::node_uid(path),
                                   fs::node_gid(path), fs::permission_bits::write);
    }

    inline long decode_unix_path(const astia_sockaddr& address, char* path,
                                 usz capacity) {
        if (address.family != AF_UNIX || address.len < 3 ||
            address.len > sizeof(address.data) + 2)
            return -EINVAL;

        const u32 length = address.len - 2;

        if (length >= capacity || address.data[0] != '/')
            return -EINVAL;

        for (u32 i = 0; i < length; ++i) {
            path[i] = static_cast<char>(address.data[i]);
        }

        path[length] = '\0';

        return 0;
    }
} // namespace astia::io::aio::operations
