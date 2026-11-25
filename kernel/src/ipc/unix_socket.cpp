#include "unix_socket.hpp"

#include <astia/api/errno.h>
#include <astia/core/spinlock.hpp>
#include <astia/mm/share.hpp>
#include <astia/resource_limits.hpp>
#include <astia/span.hpp>

#include "../fs/unix_pathname.hpp"
#include "../proc/process.hpp"
#include "../sys/aio.hpp"
#include "proc.hpp"

namespace astia::fs {
    namespace {
        constexpr u32 socket_stream_capacity_bytes = 4096;
        constexpr u32 socket_backlog_capacity      = 16;
        constexpr u32 socket_datagram_queue_capacity =
            resource_limits::queue::unix_datagrams;
        constexpr u32 socket_datagram_payload_capacity = 2048;
        constexpr u32 socket_side_first                = 0;
        constexpr u32 socket_side_second               = 1;
        constexpr u32 socket_side_count                = 2;

        constexpr u32 socket_peer_side(u32 side) {
            return side == socket_side_first ? socket_side_second : socket_side_first;
        }

        struct datagram {
            u32            len{};
            astia_sockaddr from{};
            u8             data[socket_datagram_payload_capacity]{};
        };
        struct channel {
            u8   data[socket_side_count][socket_stream_capacity_bytes]{};
            u32  head[socket_side_count]{}, tail[socket_side_count]{};
            bool closed[socket_side_count]{}, shut_rd[socket_side_count]{},
                shut_wr[socket_side_count]{};
            core::spinlock lock;
        };
        class unix_file;
        class unix_file final : public file,
                                public readable,
                                public writable,
                                public pollable {
            public:
            enum class state : u8 { fresh, listener, connected, dgram };
            unix_file() = default;

            optional<readable&> as_readable() override {
                return *this;
            }
            optional<writable&> as_writable() override {
                return *this;
            }
            optional<pollable&> as_pollable() override {
                return *this;
            }

            file_mode mode() const override {
                return file_mode(file_type::socket, file_permissions(0777));
            }
            long size() const override {
                return 0;
            }
            core::task<i64> readv(span<const buffer> buffers, u64 offset,
                                  core::cltoken token) override {
                return submit_readv(buffers, offset, token);
            }
            core::task<i64> writev(span<const buffer> buffers, u64 offset,
                                   core::cltoken token) override {
                return submit_writev(buffers, offset, token);
            }

            long read_ready(void* b, u32 n, u64) override {
                state              kind;
                mm::shptr<channel> channel;
                u32                side;
                {
                    core::spinlock_guard guard(_lock);
                    kind    = _state;
                    channel = _ch;
                    side    = _side;
                }
                if (kind == state::dgram)
                    return recv_dgram(astia::span<u8>(static_cast<u8*>(b), n));
                if (kind != state::connected)
                    return -ENOTCONN;
                if (!channel)
                    return -ENOTCONN;
                u32 count;
                {
                    core::spinlock_guard channel_guard(channel->lock);
                    if (channel->shut_rd[side])
                        return 0;
                    const u32 peer = socket_peer_side(side),
                              h = __atomic_load_n(&channel->head[peer], __ATOMIC_ACQUIRE),
                              t = __atomic_load_n(&channel->tail[peer], __ATOMIC_RELAXED);
                    if (h == t)
                        return 0;
                    count   = h - t < n ? h - t : n;
                    u8* out = static_cast<u8*>(b);
                    for (u32 i = 0; i < count; ++i)
                        out[i] = channel->data[peer][(t + i) &
                                                     (socket_stream_capacity_bytes - 1)];
                    __atomic_store_n(&channel->tail[peer], t + count, __ATOMIC_RELEASE);
                }
                // Readiness may synchronously resume a parked operation.  Never
                // call into the AIO engine while the channel lock is held.
                sys::io_device_ready_context(this);
                return count;
            }
            long write_ready(const void* b, u32 n, u64) override {
                state                kind;
                mm::shptr<channel>   channel;
                mm::shptr<unix_file> peer_owner;
                u32                  side;
                {
                    core::spinlock_guard guard(_lock);
                    kind       = _state;
                    channel    = _ch;
                    peer_owner = _peer_owner;
                    side       = _side;
                }
                if (kind == state::dgram)
                    return send_dgram(
                        astia::span<const u8>(static_cast<const u8*>(b), n));
                if (kind != state::connected)
                    return -ENOTCONN;
                if (!channel)
                    return -ENOTCONN;
                const u32 peer = socket_peer_side(side);
                u32       count;
                {
                    core::spinlock_guard channel_guard(channel->lock);
                    if (channel->closed[peer] || channel->shut_rd[peer] ||
                        channel->shut_wr[side])
                        return -EPIPE;
                    u32 h    = __atomic_load_n(&channel->head[side], __ATOMIC_RELAXED),
                        t    = __atomic_load_n(&channel->tail[side], __ATOMIC_ACQUIRE);
                    u32 free = socket_stream_capacity_bytes - (h - t);
                    if (!free)
                        return 0;
                    count        = free < n ? free : n;
                    const u8* in = static_cast<const u8*>(b);
                    for (u32 i = 0; i < count; ++i)
                        channel
                            ->data[side][(h + i) & (socket_stream_capacity_bytes - 1)] =
                            in[i];
                    __atomic_store_n(&channel->head[side], h + count, __ATOMIC_RELEASE);
                }
                // As above, readiness callbacks must run after channel unlock.
                if (peer_owner)
                    sys::io_device_ready_context(peer_owner.get());
                return count;
            }
            u32 poll(u32 events, u32) override {
                state                kind;
                mm::shptr<channel>   channel;
                u32                  side;
                u32                  queue_count;
                u32                  dgram_count;
                mm::shptr<unix_file> dgram_target_owner;
                {
                    core::spinlock_guard guard(_lock);
                    kind               = _state;
                    channel            = _ch;
                    side               = _side;
                    queue_count        = _queue_count;
                    dgram_count        = _dgram_count;
                    dgram_target_owner = _dgram_target_owner;
                }

                bool dgram_space = false;
                if (dgram_target_owner && dgram_target_owner.get() != this) {
                    core::spinlock_guard target_guard(dgram_target_owner->_lock);
                    dgram_space =
                        dgram_target_owner->_dgram_count < socket_datagram_queue_capacity;
                }
                else if (kind == state::dgram) {
                    dgram_space = dgram_count < socket_datagram_queue_capacity;
                }
                u32 result = 0;
                if (kind == state::listener) {
                    if (queue_count)
                        result |= POLLIN;
                }
                else if (kind == state::dgram) {
                    if (dgram_count)
                        result |= POLLIN;
                    if (dgram_space)
                        result |= POLLOUT;
                }
                else if (kind == state::connected && channel) {
                    core::spinlock_guard channel_guard(channel->lock);
                    const u32            peer = socket_peer_side(side);
                    if (__atomic_load_n(&channel->head[peer], __ATOMIC_ACQUIRE) !=
                        __atomic_load_n(&channel->tail[peer], __ATOMIC_RELAXED))
                        result |= POLLIN;
                    if (channel->closed[side] || channel->shut_rd[side])
                        result |= POLLIN | POLLHUP;
                    if (channel->shut_wr[peer])
                        result |= POLLIN | POLLHUP;
                    if (channel->closed[peer])
                        result |= POLLERR;
                    else if (!channel->shut_wr[side] &&
                             socket_stream_capacity_bytes -
                                     (__atomic_load_n(&channel->head[side],
                                                      __ATOMIC_RELAXED) -
                                      __atomic_load_n(&channel->tail[side],
                                                      __ATOMIC_ACQUIRE)) !=
                                 0)
                        result |= POLLOUT;
                }
                return result & (events | POLLERR | POLLHUP);
            }
            long submit(io::request& r) override {
                state              kind;
                mm::shptr<channel> channel;
                u32                side;
                {
                    core::spinlock_guard guard(_lock);
                    kind    = _state;
                    channel = _ch;
                    side    = _side;
                }
                if (kind == state::dgram) {
                    long n = r.direction() == io::direction::input
                                 ? recv_dgram(astia::span<u8>(
                                       static_cast<u8*>(r.buffer()), r.length()))
                                 : send_dgram(astia::span<const u8>(
                                       static_cast<const u8*>(r.buffer()), r.length()));
                    if (n == 0 && !r.complete())
                        r.stage(1);
                    return n;
                }
                if (kind == state::listener) {
                    auto accepted = dequeue();
                    if (!accepted) {
                        r.stage(1);
                        return 0;
                    }
                    auto* current = proc::current_process();
                    if (!current)
                        return -ESRCH;
                    for (i32 fd = 3; fd < static_cast<i32>(proc::max_process_fds); ++fd) {
                        if (!current->descriptor(fd_t{fd}) &&
                            current->set_descriptor(fd_t{fd}, accepted)) {
                            r.complete(true);
                            return fd;
                        }
                    }
                    return -EMFILE;
                }
                long n           = r.direction() == io::direction::input
                                       ? read_ready(r.buffer(), r.length(), 0)
                                       : write_ready(r.buffer(), r.length(), 0);
                bool peer_closed = false;
                if (channel) {
                    core::spinlock_guard channel_guard(channel->lock);
                    const u32            peer = socket_peer_side(side);
                    peer_closed = channel->closed[peer] || channel->shut_wr[peer];
                }
                if (n == 0 && r.direction() == io::direction::input && peer_closed)
                    r.complete(true);
                else if (n == 0)
                    r.stage(1);
                return n;
            }
            const unix_socket_operations* unix_socket_capability() const override;
            void                          duplicate(u32) override {
                __atomic_fetch_add(&_opens, 1u, __ATOMIC_ACQ_REL);
            }
            void close(u32) override {
                if (__atomic_fetch_sub(&_opens, 1u, __ATOMIC_ACQ_REL) != 1)
                    return;
                bool                 bound;
                char                 path[path_max]{};
                mm::shptr<unix_file> peer;
                mm::shptr<channel>   channel;
                u32                  side;
                {
                    core::spinlock_guard guard(_lock);
                    bound = _bound;
                    for (u32 i = 0; i < path_max; ++i) {
                        path[i] = _bound_path[i];
                        if (!path[i])
                            break;
                    }
                    _bound      = false;
                    _closed     = true;
                    peer        = _peer_owner;
                    _peer_owner = {};
                    channel     = _ch;
                    side        = _side;
                }
                if (bound)
                    unregister_unix_path(path, this);
                if (channel) {
                    {
                        core::spinlock_guard guard(channel->lock);
                        channel->closed[side]  = true;
                        channel->shut_rd[side] = true;
                        channel->shut_wr[side] = true;
                    }
                    if (peer)
                        sys::io_device_ready_context(peer.get());
                }
            }
            void make_dgram() {
                core::spinlock_guard guard(_lock);
                _state = state::dgram;
            }
            void set_bound_address(const astia_sockaddr& a) {
                core::spinlock_guard guard(_lock);
                _bound_addr = a;
            }
            void set_dgram_target(const mm::shptr<file>& endpoint) {
                core::spinlock_guard guard(_lock);
                _dgram_target_owner = endpoint;
            }
            void set_dgram_address() {
                core::spinlock_guard guard(_lock);
                _dgram_out_pending = true;
            }
            bool take_dgram_address(astia_sockaddr& out) {
                core::spinlock_guard guard(_lock);
                if (_dgram_out_pending)
                    return false;
                out = _dgram_out_value;
                return true;
            }
            long send_dgram(astia::span<const u8> payload) {
                if (payload.size() > socket_datagram_payload_capacity)
                    return -EINVAL;
                if (payload.data() == nullptr && !payload.empty())
                    return -EFAULT;
                mm::shptr<unix_file> target_owner;
                astia_sockaddr       from;
                {
                    core::spinlock_guard guard(_lock);
                    target_owner = _dgram_target_owner;
                    from         = _bound_addr;
                }
                unix_file* target = target_owner.get();
                if (!target)
                    return -ENOTCONN;
                {
                    core::spinlock_guard guard(target->_lock);
                    if (target->_state != state::dgram || target->_closed)
                        return -ENOTCONN;
                    if (target->_dgram_count == socket_datagram_queue_capacity)
                        return 0;
                    datagram& m = target->_dgrams[target->_dgram_tail];
                    m.len       = static_cast<u32>(payload.size());
                    m.from      = from;
                    for (u32 i = 0; i < m.len; ++i)
                        m.data[i] = payload.data()[i];
                    target->_dgram_tail =
                        (target->_dgram_tail + 1) % socket_datagram_queue_capacity;
                    ++target->_dgram_count;
                }
                sys::io_device_ready_context(target);
                return static_cast<long>(payload.size());
            }
            long recv_dgram(astia::span<u8> output) {
                if (output.data() == nullptr && !output.empty())
                    return -EFAULT;
                mm::shptr<unix_file> target_owner;
                u32                  count;
                {
                    core::spinlock_guard guard(_lock);
                    if (!_dgram_count)
                        return _shut_rd ? 0 : 0;
                    datagram& m = _dgrams[_dgram_head];
                    count =
                        output.size() < m.len ? static_cast<u32>(output.size()) : m.len;
                    for (u32 i = 0; i < count; ++i)
                        output.data()[i] = m.data[i];
                    if (_dgram_out_pending) {
                        _dgram_out_value   = m.from;
                        _dgram_out_pending = false;
                    }
                    _dgram_head = (_dgram_head + 1) % socket_datagram_queue_capacity;
                    --_dgram_count;
                    target_owner = _dgram_target_owner;
                }
                if (target_owner)
                    sys::io_device_ready_context(target_owner.get());
                return count;
            }
            void attach(const mm::shptr<channel>& c, u32 s,
                        const mm::shptr<unix_file>& peer) {
                core::spinlock_guard guard(_lock);
                _ch         = c;
                _side       = s;
                _peer_owner = peer;
                _state      = state::connected;
            }
            void detach_pending() {
                core::spinlock_guard guard(_lock);
                _ch         = {};
                _peer_owner = {};
                _state      = state::fresh;
            }
            state kind() const {
                core::spinlock_guard guard(_lock);
                return _state;
            }
            void make_listener(u32 b) {
                core::spinlock_guard guard(_lock);
                _state   = state::listener;
                _backlog = b;
            }
            void mark_bound(const char* path) {
                core::spinlock_guard guard(_lock);
                _bound = true;
                for (u32 i = 0; i < path_max; ++i) {
                    _bound_path[i] = path[i];
                    if (!path[i])
                        break;
                }
            }
            long shutdown(u32 how) {
                mm::shptr<unix_file> peer;
                mm::shptr<channel>   channel;
                u32                  side;
                {
                    core::spinlock_guard guard(_lock);
                    if (how > SHUT_RDWR)
                        return -EINVAL;
                    if (_state == state::dgram) {
                        if (how == SHUT_RD || how == SHUT_RDWR)
                            _shut_rd = true;
                        peer = _dgram_target_owner;
                    }
                    else {
                        if (!_ch || _state != state::connected)
                            return -EINVAL;
                        channel = _ch;
                        side    = _side;
                        peer    = _peer_owner;
                    }
                }
                if (!channel) {
                    if (peer)
                        sys::io_device_ready_context(peer.get());
                    sys::io_device_ready_context(this);
                    return 0;
                }
                {
                    core::spinlock_guard guard(channel->lock);
                    if (how == SHUT_RD || how == SHUT_RDWR)
                        channel->shut_rd[side] = true;
                    if (how == SHUT_WR || how == SHUT_RDWR)
                        channel->shut_wr[side] = true;
                }
                if (peer)
                    sys::io_device_ready_context(peer.get());
                return 0;
            }
            bool enqueue(const mm::shptr<open_file>& f) {
                core::spinlock_guard guard(_lock);
                if (_queue_count >= _backlog)
                    return false;
                _queue[_queue_tail] = f;
                _queue_tail         = (_queue_tail + 1) % socket_backlog_capacity;
                ++_queue_count;
                guard.unlock();
                sys::io_device_ready_context(this);
                return true;
            }
            mm::shptr<open_file> dequeue() {
                core::spinlock_guard guard(_lock);
                if (!_queue_count)
                    return {};
                auto x              = _queue[_queue_head];
                _queue[_queue_head] = {};
                _queue_head         = (_queue_head + 1) % socket_backlog_capacity;
                --_queue_count;
                return x;
            }

            private:
            mm::shptr<channel>     _ch;
            u32                    _side{};
            mm::shptr<unix_file>   _peer_owner;
            mutable core::spinlock _lock;
            state                  _state{state::fresh};
            bool                   _closed{};
            u32                    _backlog{};
            bool                   _bound{};
            char                   _bound_path[path_max]{};
            mm::shptr<unix_file>   _dgram_target_owner;
            astia_sockaddr         _dgram_out_value{};
            bool                   _dgram_out_pending{};
            datagram               _dgrams[socket_datagram_queue_capacity]{};
            u32                    _dgram_head{}, _dgram_tail{}, _dgram_count{};
            bool                   _shut_rd{};
            astia_sockaddr         _bound_addr{};
            mm::shptr<open_file>   _queue[socket_backlog_capacity]{};
            u32                    _queue_head{}, _queue_tail{}, _queue_count{};
            u32                    _opens{1};
        };

        long socket_bind(open_file& o, const astia_sockaddr& a);
        long socket_listen(open_file& o, u32 b);
        long socket_accept(open_file& o, io::request& r);
        long socket_connect(open_file& o, const astia_sockaddr& a);
        long socket_shutdown(open_file& o, u32 how);
        long socket_getsockopt(open_file& o, u32 level, u32 name, u32* value);
        long socket_sendto(open_file& o, const astia_sockaddr* a);
        long socket_recvfrom(open_file& o);
        bool socket_recvfrom_address(open_file& o, astia_sockaddr& out);

        const unix_socket_operations socket_operations{
            socket_bind,    socket_listen,   socket_accept,
            socket_connect, socket_shutdown, socket_getsockopt,
            socket_sendto,  socket_recvfrom, socket_recvfrom_address};

        const unix_socket_operations* unix_file::unix_socket_capability() const {
            return &socket_operations;
        }

        bool path(const astia_sockaddr& a, char* out) {
            if (a.family != AF_UNIX || a.len < 3 || a.len > sizeof(a.data) + 2)
                return false;
            u32 n = a.len - 2;
            if (n >= path_max)
                return false;
            for (u32 i = 0; i < n; ++i)
                out[i] = static_cast<char>(a.data[i]);
            out[n] = '\0';
            return out[0] == '/';
        }
    }
    long create_unix_socket(mm::shptr<open_file>& out, u32 type) {
        if (type != SOCK_STREAM && type != SOCK_DGRAM)
            return -EAFNOSUPPORT;
        auto f = mm::share<unix_file>();
        if (!f)
            return -ENOMEM;
        if (type == SOCK_DGRAM)
            f->make_dgram();
        out = mm::share<open_file>(f, 0);
        return out ? 0 : -ENOMEM;
    }
    namespace {
        long socket_bind(open_file& o, const astia_sockaddr& a) {
            auto* f = static_cast<unix_file*>(o.parent().get());
            char  n[path_max];
            if (!f || !path(a, n))
                return -EINVAL;
            auto* current = proc::current_process();
            if (!current)
                return -ESRCH;
            mm::shptr<file> node = o.parent();
            long            rc   = register_unix_path(n, node, uid_t{current->euid()},
                                                      gid_t{current->egid()},
                                                      astia::span<char>(n, sizeof n));
            if (rc < 0)
                return rc;
            f->set_bound_address(a);
            f->mark_bound(n);
            return 0;
        }
        long socket_listen(open_file& o, u32 b) {
            auto* f = static_cast<unix_file*>(o.parent().get());
            if (!f || f->kind() != unix_file::state::fresh)
                return -EINVAL;
            f->make_listener(b > socket_backlog_capacity ? socket_backlog_capacity
                             : b                         ? b
                                                         : 1);
            return 0;
        }
        long socket_connect(open_file& o, const astia_sockaddr& a) {
            auto* c = static_cast<unix_file*>(o.parent().get());
            char  n[path_max];
            if (!c || !path(a, n))
                return -EINVAL;
            mm::shptr<unix_file> c_owner(o.parent());
            mm::shptr<file>      endpoint;
            if (lookup_unix_path(n, endpoint) < 0)
                return -ECONNREFUSED;
            auto* l = static_cast<unix_file*>(endpoint.get());
            if (c->kind() == unix_file::state::dgram) {
                if (l->kind() != unix_file::state::dgram)
                    return -ENOTSUP;
                c->set_dgram_target(endpoint);
                return 0;
            }
            if (l->kind() != unix_file::state::listener)
                return -ECONNREFUSED;
            auto ch = mm::share<channel>();
            auto s  = mm::share<unix_file>();
            if (!ch || !s)
                return -ENOMEM;
            c->attach(ch, socket_side_first, s);
            s->attach(ch, socket_side_second, c_owner);
            auto so = mm::share<open_file>(s, 0);
            if (!so || !l->enqueue(so)) {
                c->detach_pending();
                return -ECONNREFUSED;
            }
            return 0;
        }
        long socket_accept(open_file& o, io::request& r) {
            auto* l = static_cast<unix_file*>(o.parent().get());
            if (!l || l->kind() != unix_file::state::listener)
                return -EINVAL;
            return l->submit(r);
        }
    }
    long unix_socketpair(mm::shptr<open_file>& left, mm::shptr<open_file>& right,
                         u32 type) {
        if (type != SOCK_STREAM && type != SOCK_DGRAM)
            return -EPROTOTYPE;
        auto a = mm::share<unix_file>();
        auto b = mm::share<unix_file>();
        if (!a || !b)
            return -ENOMEM;
        if (type == SOCK_DGRAM) {
            a->make_dgram();
            b->make_dgram();
            a->set_dgram_target(b);
            b->set_dgram_target(a);
        }
        else {
            auto ch = mm::share<channel>();
            if (!ch)
                return -ENOMEM;
            a->attach(ch, socket_side_first, b);
            b->attach(ch, socket_side_second, a);
        }
        left  = mm::share<open_file>(a, 0);
        right = mm::share<open_file>(b, 0);
        if (left && right)
            return 0;
        left  = {};
        right = {};
        return -ENOMEM;
    }
    namespace {
        long socket_shutdown(open_file& o, u32 how) {
            auto* f = static_cast<unix_file*>(o.parent().get());
            return f ? f->shutdown(how) : -EBADF;
        }
        long socket_getsockopt(open_file& o, u32 level, u32 name, u32* value) {
            auto* f = static_cast<unix_file*>(o.parent().get());
            if (!f || value == nullptr ||
                (f->kind() != unix_file::state::connected &&
                 f->kind() != unix_file::state::dgram))
                return -ENOTSOCK;
            if (level != SOL_SOCKET || name != SO_SNDBUF)
                return -ENOPROTOOPT;
            *value = socket_stream_capacity_bytes;
            return 0;
        }
        long socket_sendto(open_file& o, const astia_sockaddr* a) {
            auto* f = static_cast<unix_file*>(o.parent().get());
            if (!f || f->kind() != unix_file::state::dgram)
                return -EOPNOTSUPP;
            char n[path_max];
            if (!a || !path(*a, n))
                return -EINVAL;
            mm::shptr<file> endpoint;
            if (lookup_unix_path(n, endpoint) < 0)
                return -ECONNREFUSED;
            auto* t = static_cast<unix_file*>(endpoint.get());
            if (!t || t->kind() != unix_file::state::dgram)
                return -ECONNREFUSED;
            f->set_dgram_target(endpoint);
            return 0;
        }
        long socket_recvfrom(open_file& o) {
            auto* f = static_cast<unix_file*>(o.parent().get());
            if (!f || f->kind() != unix_file::state::dgram)
                return -EOPNOTSUPP;
            f->set_dgram_address();
            return 0;
        }
        bool socket_recvfrom_address(open_file& o, astia_sockaddr& out) {
            auto* f = static_cast<unix_file*>(o.parent().get());
            if (!f || f->kind() != unix_file::state::dgram)
                return false;
            return f->take_dgram_address(out);
        }
    }
}
