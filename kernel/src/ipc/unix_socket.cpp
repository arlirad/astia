#include "unix_socket.hpp"

#include "../proc/process.hpp"
#include "../sys/aio.hpp"
#include "proc.hpp"
#include "../fs/unix_pathname.hpp"
#include <astia/api/errno.h>
#include <astia/core/spinlock.hpp>
#include <astia/mm/share.hpp>

namespace astia::fs {
    namespace {
        constexpr u32 socket_stream_capacity_bytes    = 4096;
        constexpr u32 socket_backlog_capacity          = 16;
        constexpr u32 socket_datagram_queue_capacity  = 32;
        constexpr u32 socket_datagram_payload_capacity = 2048;
        constexpr u32 socket_side_first               = 0;
        constexpr u32 socket_side_second              = 1;
        constexpr u32 socket_side_count               = 2;

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
        class unix_file final : public file {
            public:
            enum class state : u8 { fresh, listener, connected, dgram };
            unix_file() = default;
            file_mode mode() const override {
                return file_mode(file_type::socket, file_permissions(0777));
            }
            long size() const override {
                return 0;
            }
            long read(void* b, u32 n, u64) override {
                state kind;
                mm::shptr<channel> channel;
                u32 side;
                {
                    core::spinlock_guard guard(lock_);
                    kind = state_;
                    channel = ch_;
                    side = side_;
                }
                if (kind == state::dgram)
                    return recv_dgram(static_cast<u8*>(b), n);
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
                    count = h - t < n ? h - t : n;
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
            long write(const void* b, u32 n, u64) override {
                state kind;
                mm::shptr<channel> channel;
                mm::shptr<unix_file> peer_owner;
                u32 side;
                {
                    core::spinlock_guard guard(lock_);
                    kind = state_;
                    channel = ch_;
                    peer_owner = peer_owner_;
                    side = side_;
                }
                if (kind == state::dgram)
                    return send_dgram(static_cast<const u8*>(b), n);
                if (kind != state::connected)
                    return -ENOTCONN;
                if (!channel)
                    return -ENOTCONN;
                const u32 peer = socket_peer_side(side);
                u32 count;
                {
                    core::spinlock_guard channel_guard(channel->lock);
                    if (channel->closed[peer] || channel->shut_rd[peer] ||
                        channel->shut_wr[side])
                        return -EPIPE;
                    u32 h = __atomic_load_n(&channel->head[side], __ATOMIC_RELAXED),
                        t = __atomic_load_n(&channel->tail[side], __ATOMIC_ACQUIRE);
                    u32 free = socket_stream_capacity_bytes - (h - t);
                    if (!free)
                        return 0;
                    count = free < n ? free : n;
                    const u8* in = static_cast<const u8*>(b);
                    for (u32 i = 0; i < count; ++i)
                        channel->data[side][(h + i) &
                                             (socket_stream_capacity_bytes - 1)] = in[i];
                    __atomic_store_n(&channel->head[side], h + count, __ATOMIC_RELEASE);
                }
                // As above, readiness callbacks must run after channel unlock.
                if (peer_owner)
                    sys::io_device_ready_context(peer_owner.get());
                return count;
            }
            u32 poll(u32 events, u32) override {
                state kind;
                mm::shptr<channel> channel;
                u32 side;
                u32 queue_count;
                u32 dgram_count;
                mm::shptr<unix_file> dgram_target_owner;
                {
                    core::spinlock_guard guard(lock_);
                    kind = state_;
                    channel = ch_;
                    side = side_;
                    queue_count = queue_count_;
                    dgram_count = dgram_count_;
                    dgram_target_owner = dgram_target_owner_;
                }

                bool dgram_space = false;
                if (dgram_target_owner && dgram_target_owner.get() != this) {
                    core::spinlock_guard target_guard(dgram_target_owner->lock_);
                    dgram_space =
                        dgram_target_owner->dgram_count_ < socket_datagram_queue_capacity;
                } else if (kind == state::dgram) {
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
                    const u32 peer = socket_peer_side(side);
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
                                  (__atomic_load_n(&channel->head[side], __ATOMIC_RELAXED) -
                                      __atomic_load_n(&channel->tail[side],
                                                      __ATOMIC_ACQUIRE)) !=
                                  0)
                        result |= POLLOUT;
                }
                return result & (events | POLLERR | POLLHUP);
            }
            long submit(io_request& r) override {
                state kind;
                mm::shptr<channel> channel;
                u32 side;
                {
                    core::spinlock_guard guard(lock_);
                    kind = state_;
                    channel = ch_;
                    side = side_;
                }
                if (kind == state::dgram) {
                    long n =
                        r.direction() == io_direction::input
                            ? recv_dgram(static_cast<u8*>(r.buffer()), r.length())
                            : send_dgram(static_cast<const u8*>(r.buffer()), r.length());
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
                long n = r.direction() == io_direction::input
                             ? read(r.buffer(), r.length(), 0)
                             : write(r.buffer(), r.length(), 0);
                bool peer_closed = false;
                if (channel) {
                    core::spinlock_guard channel_guard(channel->lock);
                    const u32 peer = socket_peer_side(side);
                    peer_closed = channel->closed[peer] || channel->shut_wr[peer];
                }
                if (n == 0 && r.direction() == io_direction::input && peer_closed)
                    r.complete(true);
                else if (n == 0)
                    r.stage(1);
                return n;
            }
            const void*                   capability(file_capability kind) const override;
            const unix_socket_operations* unix_socket_capability() const override;
            void                          duplicate(u32) override {
                __atomic_fetch_add(&opens_, 1u, __ATOMIC_ACQ_REL);
            }
            void close(u32) override {
                if (__atomic_fetch_sub(&opens_, 1u, __ATOMIC_ACQ_REL) != 1)
                    return;
                bool bound;
                char path[path_max]{};
                mm::shptr<unix_file> peer;
                mm::shptr<channel> channel;
                u32 side;
                {
                    core::spinlock_guard guard(lock_);
                    bound = bound_;
                    for (u32 i = 0; i < path_max; ++i) {
                        path[i] = bound_path_[i];
                        if (!path[i]) break;
                    }
                    bound_ = false;
                    closed_ = true;
                    peer = peer_owner_;
                    peer_owner_ = {};
                    channel = ch_;
                    side = side_;
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
                core::spinlock_guard guard(lock_);
                state_ = state::dgram;
            }
            void set_bound_address(const astia_sockaddr& a) {
                core::spinlock_guard guard(lock_);
                bound_addr_ = a;
            }
            void set_dgram_target(const mm::shptr<file>& endpoint) {
                core::spinlock_guard guard(lock_);
                dgram_target_owner_ = endpoint;
            }
            void set_dgram_address() {
                core::spinlock_guard guard(lock_);
                dgram_out_pending_ = true;
            }
            bool take_dgram_address(astia_sockaddr& out) {
                core::spinlock_guard guard(lock_);
                if (dgram_out_pending_)
                    return false;
                out = dgram_out_value_;
                return true;
            }
            long send_dgram(const u8* b, u32 n) {
                if (n > socket_datagram_payload_capacity)
                    return -EINVAL;
                mm::shptr<unix_file> target_owner;
                astia_sockaddr from;
                {
                    core::spinlock_guard guard(lock_);
                    target_owner = dgram_target_owner_;
                    from = bound_addr_;
                }
                unix_file* target = target_owner.get();
                if (!target)
                    return -ENOTCONN;
                {
                    core::spinlock_guard guard(target->lock_);
                    if (target->state_ != state::dgram || target->closed_)
                        return -ENOTCONN;
                    if (target->dgram_count_ == socket_datagram_queue_capacity)
                        return 0;
                    datagram& m = target->dgrams_[target->dgram_tail_];
                    m.len = n;
                    m.from = from;
                    for (u32 i = 0; i < n; ++i) m.data[i] = b[i];
                    target->dgram_tail_ =
                        (target->dgram_tail_ + 1) % socket_datagram_queue_capacity;
                    ++target->dgram_count_;
                }
                sys::io_device_ready_context(target);
                return n;
            }
            long recv_dgram(u8* b, u32 n) {
                mm::shptr<unix_file> target_owner;
                u32 count;
                {
                    core::spinlock_guard guard(lock_);
                    if (!dgram_count_)
                        return shut_rd_ ? 0 : 0;
                    datagram& m = dgrams_[dgram_head_];
                    count = n < m.len ? n : m.len;
                    for (u32 i = 0; i < count; ++i)
                        b[i] = m.data[i];
                    if (dgram_out_pending_) {
                        dgram_out_value_ = m.from;
                        dgram_out_pending_ = false;
                    }
                    dgram_head_ = (dgram_head_ + 1) % socket_datagram_queue_capacity;
                    --dgram_count_;
                    target_owner = dgram_target_owner_;
                }
                if (target_owner)
                    sys::io_device_ready_context(target_owner.get());
                return count;
            }
            void attach(const mm::shptr<channel>& c, u32 s,
                        const mm::shptr<unix_file>& peer) {
                core::spinlock_guard guard(lock_);
                ch_        = c;
                side_      = s;
                peer_owner_ = peer;
                state_     = state::connected;
            }
            void detach_pending() {
                core::spinlock_guard guard(lock_);
                ch_        = {};
                peer_owner_ = {};
                state_     = state::fresh;
            }
            state kind() const {
                core::spinlock_guard guard(lock_);
                return state_;
            }
            void make_listener(u32 b) {
                core::spinlock_guard guard(lock_);
                state_   = state::listener;
                backlog_ = b;
            }
            void mark_bound(const char* path) {
                core::spinlock_guard guard(lock_);
                bound_ = true;
                for (u32 i = 0; i < path_max; ++i) {
                    bound_path_[i] = path[i];
                    if (!path[i]) break;
                }
            }
            long shutdown(u32 how) {
                mm::shptr<unix_file> peer;
                mm::shptr<channel> channel;
                u32 side;
                {
                    core::spinlock_guard guard(lock_);
                    if (how > SHUT_RDWR)
                        return -EINVAL;
                    if (state_ == state::dgram) {
                        if (how == SHUT_RD || how == SHUT_RDWR) shut_rd_ = true;
                        peer = dgram_target_owner_;
                    } else {
                        if (!ch_ || state_ != state::connected)
                            return -EINVAL;
                        channel = ch_;
                        side = side_;
                        peer = peer_owner_;
                    }
                }
                if (!channel) {
                    if (peer) sys::io_device_ready_context(peer.get());
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
                if (peer) sys::io_device_ready_context(peer.get());
                return 0;
            }
            bool enqueue(const mm::shptr<open_file>& f) {
                core::spinlock_guard guard(lock_);
                if (queue_count_ >= backlog_)
                    return false;
                queue_[queue_tail_] = f;
                queue_tail_         = (queue_tail_ + 1) % socket_backlog_capacity;
                ++queue_count_;
                guard.unlock();
                sys::io_device_ready_context(this);
                return true;
            }
            mm::shptr<open_file> dequeue() {
                core::spinlock_guard guard(lock_);
                if (!queue_count_)
                    return {};
                auto x              = queue_[queue_head_];
                queue_[queue_head_] = {};
                queue_head_         = (queue_head_ + 1) % socket_backlog_capacity;
                --queue_count_;
                return x;
            }

            private:
            mm::shptr<channel>   ch_;
            u32                  side_{};
            mm::shptr<unix_file> peer_owner_;
            mutable core::spinlock lock_;
            state                state_{state::fresh};
            bool                 closed_{};
            u32                  backlog_{};
            bool                 bound_{};
            char                 bound_path_[path_max]{};
            mm::shptr<unix_file> dgram_target_owner_;
            astia_sockaddr       dgram_out_value_{};
            bool                 dgram_out_pending_{};
            datagram             dgrams_[socket_datagram_queue_capacity]{};
            u32                  dgram_head_{}, dgram_tail_{}, dgram_count_{};
            bool                 shut_rd_{};
            astia_sockaddr       bound_addr_{};
            mm::shptr<open_file> queue_[socket_backlog_capacity]{};
            u32                  queue_head_{}, queue_tail_{}, queue_count_{};
            u32                  opens_{1};
        };

        long unix_read(file* object, void* buffer, u32 length, u64 offset) {
            return static_cast<unix_file*>(object)->read(buffer, length, offset);
        }
        long unix_write(file* object, const void* buffer, u32 length, u64 offset) {
            return static_cast<unix_file*>(object)->write(buffer, length, offset);
        }
        long unix_submit(file* object, io_request& request) {
            return static_cast<unix_file*>(object)->submit(request);
        }
        u32 unix_poll(const file* object, u32 events, u32 flags) {
            return const_cast<unix_file*>(static_cast<const unix_file*>(object))
                ->poll(events, flags);
        }

        const file_read_capability   unix_read_capability{unix_read};
        const file_write_capability  unix_write_capability{unix_write};
        const file_submit_capability unix_submit_capability{unix_submit};
        const file_poll_capability   unix_poll_capability{unix_poll};

        long socket_bind(open_file& o, const astia_sockaddr& a);
        long socket_listen(open_file& o, u32 b);
        long socket_accept(open_file& o, io_request& r);
        long socket_connect(open_file& o, const astia_sockaddr& a);
        long socket_shutdown(open_file& o, u32 how);
        long socket_sendto(open_file& o, const astia_sockaddr* a);
        long socket_recvfrom(open_file& o);
        bool socket_recvfrom_address(open_file& o, astia_sockaddr& out);

        const unix_socket_operations socket_operations{
            socket_bind,     socket_listen, socket_accept,   socket_connect,
            socket_shutdown, socket_sendto, socket_recvfrom, socket_recvfrom_address};

        const void* unix_file::capability(file_capability kind) const {
            switch (kind) {
            case file_capability::read:
                return &unix_read_capability;
            case file_capability::write:
                return &unix_write_capability;
            case file_capability::submit:
                return &unix_submit_capability;
            case file_capability::poll:
                return &unix_poll_capability;
            default:
                return file::capability(kind);
            }
        }

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
            auto* f = static_cast<unix_file*>(o.backing());
            char  n[path_max];
            if (!f || !path(a, n))
                return -EINVAL;
            char canonical[path_max]{};
            auto* current = proc::current_process();
            if (!current)
                return -ESRCH;
            mm::shptr<file> node = o.node();
            long rc = register_unix_path(n, node, uid_t{current->euid()},
                                         gid_t{current->egid()}, canonical, sizeof canonical);
            if (rc < 0)
                return rc;
            f->set_bound_address(a);
            f->mark_bound(canonical);
            return 0;
        }
        long socket_listen(open_file& o, u32 b) {
            auto* f = static_cast<unix_file*>(o.backing());
            if (!f || f->kind() != unix_file::state::fresh)
                return -EINVAL;
            f->make_listener(b > socket_backlog_capacity
                                 ? socket_backlog_capacity
                                 : b ? b : 1);
            return 0;
        }
        long socket_connect(open_file& o, const astia_sockaddr& a) {
            auto* c = static_cast<unix_file*>(o.backing());
            char  n[path_max];
            if (!c || !path(a, n))
                return -EINVAL;
            mm::shptr<unix_file> c_owner(o.node());
            mm::shptr<file> endpoint;
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
        long socket_accept(open_file& o, io_request& r) {
            auto* l = static_cast<unix_file*>(o.backing());
            if (!l || l->kind() != unix_file::state::listener)
                return -EINVAL;
            return l->submit(r);
        }
    }
    long unix_socketpair(mm::shptr<open_file>& left, mm::shptr<open_file>& right,
                         u32 type) {
        if (type != SOCK_STREAM)
            return -EAFNOSUPPORT;
        auto ch = mm::share<channel>();
        auto a  = mm::share<unix_file>();
        auto b  = mm::share<unix_file>();
        if (!ch || !a || !b)
            return -ENOMEM;
         a->attach(ch, socket_side_first, b);
         b->attach(ch, socket_side_second, a);
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
            auto* f = static_cast<unix_file*>(o.backing());
            return f ? f->shutdown(how) : -EBADF;
        }
        long socket_sendto(open_file& o, const astia_sockaddr* a) {
            auto* f = static_cast<unix_file*>(o.backing());
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
            auto* f = static_cast<unix_file*>(o.backing());
            if (!f || f->kind() != unix_file::state::dgram)
                return -EOPNOTSUPP;
            f->set_dgram_address();
            return 0;
        }
        bool socket_recvfrom_address(open_file& o, astia_sockaddr& out) {
            auto* f = static_cast<unix_file*>(o.backing());
            if (!f || f->kind() != unix_file::state::dgram)
                return false;
            return f->take_dgram_address(out);
        }
    }
}
