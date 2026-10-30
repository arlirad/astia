#include "unix_socket.hpp"

#include "../proc/process.hpp"
#include "../sys/aio.hpp"
#include "proc.hpp"
#include <astia/api/errno.h>
#include <astia/core/spinlock.hpp>
#include <astia/mm/share.hpp>

namespace astia::fs {
    namespace {
        constexpr u32 socket_stream_capacity_bytes    = 4096;
        constexpr u32 socket_name_capacity             = 32;
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
        };
        class unix_file;
        struct name_entry {
            bool       used{};
            char       name[path_max]{};
            unix_file* listener{};
            open_file* owner{};
        };
        name_entry     names[socket_name_capacity];
        core::spinlock names_lock;

        class unix_file final : public file {
            public:
            enum class state : u8 { fresh, listener, connected, dgram };
            unix_file() = default;
            long read(void* b, u32 n, u64) override {
                if (state_ == state::dgram)
                    return recv_dgram(static_cast<u8*>(b), n);
                if (state_ != state::connected)
                    return -ENOTCONN;
                if (!ch_)
                    return -ENOTCONN;
                if (ch_->shut_rd[side_])
                    return 0;
                const u32 side = socket_peer_side(side_),
                          h    = __atomic_load_n(&ch_->head[side], __ATOMIC_ACQUIRE),
                          t    = __atomic_load_n(&ch_->tail[side], __ATOMIC_RELAXED);
                if (h == t)
                    return ch_->closed[socket_peer_side(side_)] ||
                                   ch_->shut_wr[socket_peer_side(side_)]
                               ? 0
                               : 0;
                u32 count = h - t < n ? h - t : n;
                u8* out   = static_cast<u8*>(b);
                for (u32 i = 0; i < count; ++i)
                    out[i] = ch_->data[side][(t + i) & (socket_stream_capacity_bytes - 1)];
                __atomic_store_n(&ch_->tail[side], t + count, __ATOMIC_RELEASE);
                sys::io_device_ready_context(this);
                return count;
            }
            long write(const void* b, u32 n, u64) override {
                if (state_ == state::dgram)
                    return send_dgram(static_cast<const u8*>(b), n);
                if (state_ != state::connected)
                    return -ENOTCONN;
                if (!ch_)
                    return -ENOTCONN;
                const u32 side = side_, peer = socket_peer_side(side_);
                if (ch_->closed[peer] || ch_->shut_rd[peer] || ch_->shut_wr[side_])
                    return -EPIPE;
                u32 h    = __atomic_load_n(&ch_->head[side], __ATOMIC_RELAXED),
                    t    = __atomic_load_n(&ch_->tail[side], __ATOMIC_ACQUIRE);
                u32 free = socket_stream_capacity_bytes - (h - t);
                if (!free)
                    return 0;
                u32       count = free < n ? free : n;
                const u8* in    = static_cast<const u8*>(b);
                for (u32 i = 0; i < count; ++i)
                    ch_->data[side][(h + i) & (socket_stream_capacity_bytes - 1)] = in[i];
                __atomic_store_n(&ch_->head[side], h + count, __ATOMIC_RELEASE);
                sys::io_device_ready_context(peer_file_);
                return count;
            }
            u32 poll(u32 events, u32) override {
                u32 result = 0;
                if (state_ == state::listener) {
                    if (queue_count_)
                        result |= POLLIN;
                }
                else if (state_ == state::dgram) {
                    if (dgram_count_)
                        result |= POLLIN;
                    if (dgram_target_ &&
                        dgram_target_->dgram_count_ < socket_datagram_queue_capacity)
                        result |= POLLOUT;
                }
                else if (state_ == state::connected && ch_) {
                    const u32 peer = socket_peer_side(side_);
                    if (__atomic_load_n(&ch_->head[peer], __ATOMIC_ACQUIRE) !=
                        __atomic_load_n(&ch_->tail[peer], __ATOMIC_RELAXED))
                        result |= POLLIN;
                    if (ch_->closed[side_] || ch_->shut_rd[side_])
                        result |= POLLIN | POLLHUP;
                    if (ch_->shut_wr[peer])
                        result |= POLLIN | POLLHUP;
                    if (ch_->closed[peer])
                        result |= POLLERR;
                    else if (!ch_->shut_wr[side_] &&
                              socket_stream_capacity_bytes -
                                  (__atomic_load_n(&ch_->head[side_], __ATOMIC_RELAXED) -
                                     __atomic_load_n(&ch_->tail[side_],
                                                     __ATOMIC_ACQUIRE)) !=
                                 0)
                        result |= POLLOUT;
                }
                return result & (events | POLLERR | POLLHUP);
            }
            long submit(io_request& r) override {
                if (state_ == state::dgram) {
                    long n =
                        r.direction() == io_direction::input
                            ? recv_dgram(static_cast<u8*>(r.buffer()), r.length())
                            : send_dgram(static_cast<const u8*>(r.buffer()), r.length());
                    if (n == 0 && !r.complete())
                        r.stage(1);
                    return n;
                }
                if (state_ == state::listener) {
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
                if (n == 0 && r.direction() == io_direction::input && ch_ &&
                    (ch_->closed[socket_peer_side(side_)] ||
                     ch_->shut_wr[socket_peer_side(side_)]))
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
                if (bound_) {
                    core::spinlock_guard g(names_lock);
                    for (auto& e : names)
                        if (e.listener == this)
                            e = name_entry{};
                    bound_ = false;
                }
                if (ch_) {
                    ch_->closed[side_]  = true;
                    ch_->shut_rd[side_] = true;
                    ch_->shut_wr[side_] = true;
                    unix_file* peer     = peer_file_;
                    peer_file_          = nullptr;
                    if (peer) {
                        peer->peer_file_ = nullptr;
                        sys::io_device_ready_context(peer);
                    }
                }
            }
            void make_dgram() {
                state_ = state::dgram;
            }
            void set_bound_address(const astia_sockaddr& a) {
                bound_addr_ = a;
            }
            void set_dgram_target(const mm::shptr<open_file>& owner) {
                dgram_target_owner_ = owner;
                dgram_target_ =
                    owner ? static_cast<unix_file*>(owner->backing()) : nullptr;
            }
            void set_dgram_address() {
                dgram_out_pending_ = true;
            }
            bool take_dgram_address(astia_sockaddr& out) {
                if (dgram_out_pending_)
                    return false;
                out = dgram_out_value_;
                return true;
            }
            long send_dgram(const u8* b, u32 n) {
                unix_file* target = dgram_target_;
                if (!target || target->state_ != state::dgram)
                    return -ENOTCONN;
                if (n > socket_datagram_payload_capacity)
                    return -EINVAL;
                if (target->dgram_count_ == socket_datagram_queue_capacity)
                    return 0;
                datagram& m = target->dgrams_[target->dgram_tail_];
                m.len       = n;
                m.from      = bound_addr_;
                for (u32 i = 0; i < n; ++i)
                    m.data[i] = b[i];
                target->dgram_tail_ =
                    (target->dgram_tail_ + 1) % socket_datagram_queue_capacity;
                ++target->dgram_count_;
                sys::io_device_ready_context(target);
                return n;
            }
            long recv_dgram(u8* b, u32 n) {
                if (!dgram_count_)
                    return shut_rd_ ? 0 : 0;
                datagram& m     = dgrams_[dgram_head_];
                u32       count = n < m.len ? n : m.len;
                for (u32 i = 0; i < count; ++i)
                    b[i] = m.data[i];
                if (dgram_out_pending_) {
                    dgram_out_value_   = m.from;
                    dgram_out_pending_ = false;
                }
                dgram_head_ = (dgram_head_ + 1) % socket_datagram_queue_capacity;
                --dgram_count_;
                if (dgram_target_)
                    sys::io_device_ready_context(dgram_target_);
                return count;
            }
            void attach(const mm::shptr<channel>& c, u32 s, unix_file* peer) {
                ch_        = c;
                side_      = s;
                peer_file_ = peer;
                state_     = state::connected;
            }
            void detach_pending() {
                ch_        = {};
                peer_file_ = nullptr;
                state_     = state::fresh;
            }
            state kind() const {
                return state_;
            }
            void make_listener(u32 b) {
                state_   = state::listener;
                backlog_ = b;
            }
            void mark_bound() {
                bound_ = true;
            }
            long shutdown(u32 how) {
                if (state_ == state::dgram) {
                    if (how > SHUT_RDWR)
                        return -EINVAL;
                    if (how == SHUT_RD || how == SHUT_RDWR)
                        shut_rd_ = true;
                    if (dgram_target_)
                        sys::io_device_ready_context(dgram_target_);
                    sys::io_device_ready_context(this);
                    return 0;
                }
                if (!ch_ || state_ != state::connected || how > SHUT_RDWR)
                    return -EINVAL;
                if (how == SHUT_RD || how == SHUT_RDWR)
                    ch_->shut_rd[side_] = true;
                if (how == SHUT_WR || how == SHUT_RDWR)
                    ch_->shut_wr[side_] = true;
                if (peer_file_)
                    sys::io_device_ready_context(peer_file_);
                return 0;
            }
            bool enqueue(const mm::shptr<open_file>& f) {
                if (queue_count_ >= backlog_)
                    return false;
                queue_[queue_tail_] = f;
                queue_tail_         = (queue_tail_ + 1) % socket_backlog_capacity;
                ++queue_count_;
                sys::io_device_ready_context(this);
                return true;
            }
            mm::shptr<open_file> dequeue() {
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
            unix_file*           peer_file_{};
            state                state_{state::fresh};
            u32                  backlog_{};
            bool                 bound_{};
            mm::shptr<open_file> dgram_target_owner_;
            unix_file*           dgram_target_{};
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
        bool same(const char* a, const char* b) {
            if (!a || !b)
                return false;
            while (*a && *a == *b) {
                ++a;
                ++b;
            }
            return *a == *b;
        }
        unix_file* find(const char* n) {
            for (auto& e : names)
                if (e.used && e.listener && same(e.name, n))
                    return e.listener;
            return nullptr;
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
            core::spinlock_guard g(names_lock);
            if (find(n))
                return -EADDRINUSE;
            for (auto& e : names)
                if (!e.used) {
                    e.used     = true;
                    e.listener = f;
                    e.owner    = &o;
                    for (u32 i = 0; i < path_max; ++i) {
                        e.name[i] = n[i];
                        if (!n[i])
                            break;
                    }
                    f->set_bound_address(a);
                    f->mark_bound();
                    return 0;
                }
            return -ENFILE;
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
            core::spinlock_guard g(names_lock);
            auto*                l = find(n);
            if (!l)
                return -ECONNREFUSED;
            if (c->kind() == unix_file::state::dgram) {
                if (l->kind() != unix_file::state::dgram)
                    return -ENOTSUP;
                for (auto& e : names)
                    if (e.listener == l && e.owner) {
                        auto owner = mm::share<open_file>(*e.owner);
                        if (!owner)
                            return -ENOMEM;
                        c->set_dgram_target(owner);
                        return 0;
                    }
                return -ECONNREFUSED;
            }
            if (l->kind() != unix_file::state::listener)
                return -ECONNREFUSED;
            auto ch = mm::share<channel>();
            auto s  = mm::share<unix_file>();
            if (!ch || !s)
                return -ENOMEM;
            c->attach(ch, socket_side_first, s.get());
            s->attach(ch, socket_side_second, c);
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
        a->attach(ch, socket_side_first, b.get());
        b->attach(ch, socket_side_second, a.get());
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
            auto* t = find(n);
            if (!t || t->kind() != unix_file::state::dgram)
                return -ECONNREFUSED;
            for (auto& e : names)
                if (e.listener == t && e.owner) {
                    auto owner = mm::share<open_file>(*e.owner);
                    if (!owner)
                        return -ENOMEM;
                    f->set_dgram_target(owner);
                    return 0;
                }
            return -ECONNREFUSED;
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
