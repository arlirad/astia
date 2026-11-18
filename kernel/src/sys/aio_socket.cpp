#include "aio_socket.hpp"

#include "aio_dispatch.hpp"

#include "../fs/mapping.hpp"
#include "../ipc/pipe.hpp"
#include "../ipc/unix_socket.hpp"
#include "../proc/mapping.hpp"
#include "proc.hpp"
#include "user_mem.hpp"
#include <astia/api/errno.h>
#include <astia/mapping.hpp>

namespace astia::sys::aio_socket {

    using namespace aio_ring;
    using aio_dispatch::file_for;
    using aio_dispatch::parent_writable;

    namespace {

        constexpr usize max_operation_bytes = ASTIA_AIO_MAX_OPERATION_BYTES;

        bool socket_node_writable(proc::process* current, const char* path) {
            if (!current || !path)
                return false;
            const fs::file_type type = fs::node_type(path);
            if (type == fs::directory_unknown || type != fs::directory_sock)
                return true;
            return current->can_access(fs::node_mode(path), fs::node_uid(path),
                                       fs::node_gid(path), fs::permission_bits::write);
        }

        bool handle_pipe(astia_rings* ring, const astia_sqe& s) {
            const auto out = sys::user_ptr<i32>(s.address).validate(2);
            if (!out)
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            proc::process* current = proc::current_process();
            if (current == nullptr)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            auto pipe = fs::create_pipe();
            if (!pipe)
                return cq_post_guarded(ring, s.user_data,
                                       static_cast<i32>(-astia::errno_value(pipe.error)));
            auto& read_end  = pipe.value.first;
            auto& write_end = pipe.value.second;
            i32   read_fd   = -1;
            i32   write_fd  = -1;
            for (i32 fd = 3; fd < static_cast<i32>(proc::max_process_fds); ++fd) {
                if (read_fd < 0 && !current->descriptor(fd_t{fd})) {
                    if (current->set_descriptor(fd_t{fd}, read_end))
                        read_fd = fd;
                }
                else if (write_fd < 0 && !current->descriptor(fd_t{fd})) {
                    if (current->set_descriptor(fd_t{fd}, write_end)) {
                        write_fd = fd;
                        break;
                    }
                }
            }
            if (read_fd < 0 || write_fd < 0) {
                if (read_fd >= 0)
                    current->clear_descriptor(fd_t{read_fd});
                return cq_post_guarded(ring, s.user_data, -EMFILE);
            }
            i32 result[2] = {read_fd, write_fd};
            if (!sys::copy_out(out.value.bytes(2), result)) {
                current->clear_descriptor(fd_t{read_fd});
                current->clear_descriptor(fd_t{write_fd});
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            }
            return cq_post_guarded(ring, s.user_data, 0);
        }

        enum class socket_domain : u32 {
            unix_domain = AF_UNIX,
        };
        enum class socket_type : u32 {
            stream   = SOCK_STREAM,
            datagram = SOCK_DGRAM,
        };
        struct socket_request {
            socket_domain domain;
            socket_type   type;
            u32           aux;

            static constexpr socket_request from_sqe(const astia_sqe& s) {
                return {static_cast<socket_domain>(static_cast<u32>(s.fd)),
                        static_cast<socket_type>(s.op_flags), s.aux};
            }

            constexpr bool supported() const {
                return domain == socket_domain::unix_domain &&
                       (type == socket_type::stream || type == socket_type::datagram) &&
                       aux == 0;
            }
        };

        static_assert(static_cast<u32>(socket_domain::unix_domain) == AF_UNIX);
        static_assert(static_cast<u32>(socket_type::stream) == SOCK_STREAM);
        static_assert(static_cast<u32>(socket_type::datagram) == SOCK_DGRAM);

        bool handle_socket(astia_rings* ring, const astia_sqe& s) {
            const socket_request request = socket_request::from_sqe(s);
            if (!request.supported())
                return cq_post_guarded(ring, s.user_data, -EAFNOSUPPORT);
            proc::process*           current = proc::current_process();
            mm::shptr<fs::open_file> object;
            long rc = fs::create_unix_socket(object, static_cast<u32>(request.type));
            if (rc < 0 || !current)
                return cq_post_guarded(ring, s.user_data, (i32) (rc < 0 ? rc : -ESRCH));
            for (i32 fd = 3; fd < (i32) proc::max_process_fds; ++fd)
                if (!current->descriptor(fd_t{fd}) &&
                    current->set_descriptor(fd_t{fd}, object))
                    return cq_post_guarded(ring, s.user_data, fd);
            return cq_post_guarded(ring, s.user_data, -EMFILE);
        }

        bool handle_socketpair(astia_rings* ring, const astia_sqe& s) {
            if (s.fd != AF_UNIX)
                return cq_post_guarded(ring, s.user_data, -EAFNOSUPPORT);
            if (s.aux != 0)
                return cq_post_guarded(ring, s.user_data, -EPROTONOSUPPORT);
            if (s.op_flags != SOCK_STREAM && s.op_flags != SOCK_DGRAM)
                return cq_post_guarded(ring, s.user_data, -EPROTOTYPE);
            if (s.length != 2 * sizeof(i32))
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            if (!sys::user_span_writable_ok_for(
                    proc::current_process(), span<u8>::bytes(s.address, s.length)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            proc::process* current = proc::current_process();
            if (!current)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            mm::shptr<fs::open_file> a, b;
            long                     rc = fs::unix_socketpair(a, b, s.op_flags);
            if (rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) rc);
            i32 fds[2] = {-1, -1};
            for (i32 fd = 3; fd < (i32) proc::max_process_fds && fds[1] < 0; ++fd) {
                if (fds[0] < 0 && !current->descriptor(fd_t{fd}) &&
                    current->set_descriptor(fd_t{fd}, a))
                    fds[0] = fd;
                else if (fds[0] >= 0 && !current->descriptor(fd_t{fd}) &&
                         current->set_descriptor(fd_t{fd}, b))
                    fds[1] = fd;
            }
            if (fds[1] < 0) {
                if (fds[0] >= 0)
                    current->clear_descriptor(fd_t{fds[0]});
                return cq_post_guarded(ring, s.user_data, -EMFILE);
            }
            if (!sys::copy_out(span<u8>::bytes(s.address, s.length), fds)) {
                current->clear_descriptor(fd_t{fds[0]});
                current->clear_descriptor(fd_t{fds[1]});
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            }
            return cq_post_guarded(ring, s.user_data, 0);
        }

        bool handle_sendto_recvfrom(ring_slot& slot, astia_rings* ring,
                                    const astia_sqe& s) {
            mm::shptr<fs::open_file> object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            const auto address = sys::user_ptr<astia_sockaddr>(s.offset).validate();
            if (!sys::user_span_ok(span<u8>::bytes(s.address, s.length)) || !address)
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            if (s.length > max_operation_bytes)
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            astia_sockaddr a{};
            if (s.op == OP_SENDTO) {
                if (!address || !sys::copy_in(&a, address.value.bytes()))
                    return cq_post_guarded(ring, s.user_data, -EFAULT);
                char socket_path[fs::path_max]{}, canonical[fs::path_max]{};
                if (a.family != AF_UNIX || a.len < 3 || a.len > sizeof(a.data) + 2)
                    return cq_post_guarded(ring, s.user_data, -EINVAL);
                const u32 path_length = a.len - 2;
                if (path_length >= sizeof socket_path || a.data[0] != '/')
                    return cq_post_guarded(ring, s.user_data, -EINVAL);
                for (u32 i = 0; i < path_length; ++i)
                    socket_path[i] = static_cast<char>(a.data[i]);
                socket_path[path_length] = '\0';
                long path_rc =
                    fs::resolve_path(socket_path, canonical, sizeof canonical, false);
                if (path_rc < 0)
                    return cq_post_guarded(ring, s.user_data, static_cast<i32>(path_rc));
                if (!socket_node_writable(proc::current_process(), canonical))
                    return cq_post_guarded(ring, s.user_data, -EACCES);
                const auto* socket = object->node()->unix_socket_capability();
                long rc = socket == nullptr ? -EOPNOTSUPP : socket->sendto(*object, &a);
                if (rc < 0)
                    return cq_post_guarded(ring, s.user_data, static_cast<i32>(rc));
            }
            else {
                if (s.length == 0)
                    return cq_post_guarded(ring, s.user_data, 0);
                const auto* socket = object->node()->unix_socket_capability();
                long rc = socket == nullptr ? -EOPNOTSUPP : socket->recvfrom(*object);
                if (rc < 0)
                    return cq_post_guarded(ring, s.user_data, static_cast<i32>(rc));
            }
            span<u8> range = span<u8>::bytes(s.address, s.length);
            return aio_dispatch::submit_socket_request(
                slot, ring, s, object, &range, 1,
                s.op == OP_RECVFROM ? io_direction::input : io_direction::output);
        }

        bool handle_bind_connect(astia_rings* ring, const astia_sqe& s) {
            auto object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            const auto address = sys::user_ptr<astia_sockaddr>(s.address).validate();
            if (!address)
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            astia_sockaddr a{};
            if (!sys::copy_in(&a, address.value.bytes()))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            char socket_path[fs::path_max]{}, canonical[fs::path_max]{};
            if (a.family != AF_UNIX || a.len < 3 || a.len > sizeof(a.data) + 2)
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            const u32 path_length = a.len - 2;
            if (path_length >= sizeof socket_path || a.data[0] != '/')
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            for (u32 i = 0; i < path_length; ++i)
                socket_path[i] = static_cast<char>(a.data[i]);
            socket_path[path_length] = '\0';
            long path_rc =
                fs::resolve_path(socket_path, canonical, sizeof canonical, false);
            if (path_rc < 0)
                return cq_post_guarded(ring, s.user_data, static_cast<i32>(path_rc));
            if (s.op == OP_BIND) {
                if (!parent_writable(proc::current_process(), canonical))
                    return cq_post_guarded(ring, s.user_data, -EACCES);
            }
            else if (!socket_node_writable(proc::current_process(), canonical)) {
                return cq_post_guarded(ring, s.user_data, -EACCES);
            }
            const auto* socket = object->node()->unix_socket_capability();
            long rc = socket == nullptr ? -EOPNOTSUPP
                                        : (s.op == OP_BIND ? socket->bind(*object, a)
                                                           : socket->connect(*object, a));
            return cq_post_guarded(ring, s.user_data, (i32) rc);
        }

        bool handle_listen(astia_rings* ring, const astia_sqe& s) {
            auto object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            const auto* socket = object->node()->unix_socket_capability();
            return cq_post_guarded(
                ring, s.user_data,
                static_cast<i32>(socket == nullptr ? -EOPNOTSUPP
                                                   : socket->listen(*object, s.aux)));
        }

        bool handle_accept(ring_slot& slot, astia_rings* ring, const astia_sqe& s) {
            auto object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            if (object->node()->unix_socket_capability() == nullptr)
                return cq_post_guarded(ring, s.user_data, -EOPNOTSUPP);
            return aio_dispatch::submit_socket_request(
                slot, ring, s, object, nullptr, 0, io_direction::input, true, true);
        }

        bool handle_getsockopt(astia_rings* ring, const astia_sqe& s) {
            auto object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            auto length = sys::user_ptr<u32>(s.offset).validate();
            if (!length || !sys::user_span_writable_ok_for(
                              proc::current_process(), length.value.bytes()))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            u32 capacity = 0;
            if (!sys::copy_in(&capacity, length.value.bytes()))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            if (capacity > max_operation_bytes)
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            const auto* socket = object->node()->unix_socket_capability();
            if (socket == nullptr)
                return cq_post_guarded(ring, s.user_data, -ENOTSOCK);
            u32 value = 0;
            long rc = socket->getsockopt(*object, s.op_flags, s.aux, &value);
            if (rc < 0)
                return cq_post_guarded(ring, s.user_data, static_cast<i32>(rc));
            const u32 result_length = capacity < sizeof value ? capacity : sizeof value;
            if (!sys::copy_out_for(proc::current_process(),
                                   span<u8>::bytes(s.address, result_length), &value) ||
                !sys::copy_out_for(proc::current_process(), length.value.bytes(),
                                   &result_length))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            return cq_post_guarded(ring, s.user_data, 0);
        }

        bool handle_shutdown(astia_rings* ring, const astia_sqe& s) {
            auto object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            const auto* socket = object->node()->unix_socket_capability();
            return cq_post_guarded(
                ring, s.user_data,
                static_cast<i32>(socket == nullptr
                                     ? -EOPNOTSUPP
                                     : socket->shutdown(*object, s.op_flags)));
        }

    } // namespace

    bool dispatch(ring_slot& slot, astia_rings* ring, const astia_sqe& s) {
        switch (s.op) {
        case OP_PIPE:
            return handle_pipe(ring, s);
        case OP_SOCKET:
            return handle_socket(ring, s);
        case OP_BIND:
        case OP_CONNECT:
            return handle_bind_connect(ring, s);
        case OP_LISTEN:
            return handle_listen(ring, s);
        case OP_ACCEPT:
            return handle_accept(slot, ring, s);
        case OP_SENDTO:
        case OP_RECVFROM:
            return handle_sendto_recvfrom(slot, ring, s);
        case OP_SOCKETPAIR:
            return handle_socketpair(ring, s);
        case OP_SHUTDOWN:
            return handle_shutdown(ring, s);
        case OP_GETSOCKOPT:
            return handle_getsockopt(ring, s);
        default:
            return false;
        }
    }

} // namespace astia::sys::aio_socket
