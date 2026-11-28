#include "unix_socket.hpp"

#include <astia/api/errno.h>
#include <astia/atomic.hpp>
#include <astia/core/completion.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/fs/socket.hpp>
#include <astia/mm/share.hpp>
#include <astia/mm/shweak.hpp>
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
        constexpr u32 socket_control_queue_capacity    = 8;

        constexpr u32 socket_peer_side(u32 side) {
            return side == socket_side_first ? socket_side_second : socket_side_first;
        }

        void copy_path(char (&destination)[path_max], const char* source) {
            for (u32 i = 0; i < path_max; ++i) {
                destination[i] = source[i];

                if (!source[i])
                    break;
            }
        }

        struct datagram {
            u32                     len{};
            astia_sockaddr          from{};
            mm::shptr<unix_control> control;
            u8                      data[socket_datagram_payload_capacity]{};
        };

        struct channel {
            u8   data[socket_side_count][socket_stream_capacity_bytes]{};
            u32  head[socket_side_count]{}, tail[socket_side_count]{};
            bool closed[socket_side_count]{}, shut_rd[socket_side_count]{},
                shut_wr[socket_side_count]{};
            mm::shptr<unix_control> control[socket_side_count]
                                           [socket_control_queue_capacity];
            u32 control_head[socket_side_count]{}, control_tail[socket_side_count]{},
                control_count[socket_side_count]{};
            core::spinlock lock;
        };

        class unix_file;

        enum { accept_waiter_limit = 8 };

        enum class accept_waiter_state : u8 {
            free,
            waiting,
            delivering,
        };

        struct accept_waiter {
            accept_waiter*       next = nullptr;
            core::completion<u8> done{};
            u32                  refs  = 0;
            accept_waiter_state  state = accept_waiter_state::free;
        };

        class accept_scope final {
            public:
            accept_scope(unix_file& owner, accept_waiter* value);
            ~accept_scope();

            accept_scope(const accept_scope&)            = delete;
            accept_scope& operator=(const accept_scope&) = delete;

            private:
            unix_file*     _owner = nullptr;
            accept_waiter* _value = nullptr;
        };

        bool path(const astia_sockaddr& a, char* out) {
            if (a.family != AF_UNIX || a.len < 3 || a.len > sizeof(a.data) + 2)
                return false;

            u32 n = a.len - 2;
            if (n >= path_max)
                return false;

            for (u32 i = 0; i < n; ++i) {
                out[i] = static_cast<char>(a.data[i]);
            }

            out[n] = '\0';

            return out[0] == '/';
        }

        /// Public datagram capability backed by one UNIX socket.  The view
        /// exists so callers hold `fs::dgram_sock&` instead of downcasting to
        /// the concrete implementation.
        class unix_dgram_view final : public dgram_sock {
            public:
            explicit unix_dgram_view(unix_file& owner) : _owner(owner) {}

            u16 family() const override {
                return AF_UNIX;
            }

            u32 socket_type() const override {
                return SOCK_DGRAM;
            }

            long bind(const astia_sockaddr& address,
                      const char*           canonical = nullptr) override;
            long connect(const astia_sockaddr& address,
                         const char*           canonical = nullptr) override;
            long getsockopt(u32 level, u32 name, u32* value) override;

            core::task<i64> sendto(span<const u8> data, const astia_sockaddr& destination,
                                   core::cltoken token    = {},
                                   bool          nonblock = false) override;
            core::task<message> recvfrom(span<u8>      data,
                                         core::cltoken token = {}) override;
            core::task<i64>     sendmsg(span<const u8>                 data,
                                        const astia_sockaddr*          destination,
                                        const mm::shptr<unix_control>& control,
                                        core::cltoken                  token = {},
                                        bool nonblock = false) override;
            core::task<message> recvmsg(span<u8> data, core::cltoken token = {}) override;

            optional<readable&> as_readable() override;
            optional<writable&> as_writable() override;
            optional<pollable&> as_pollable() override;
            long                submit(io::request& request) override;

            private:
            unix_file& _owner;
        };

        /// Public stream capability for the same implementation.
        class unix_stream_view final : public stream_sock {
            public:
            explicit unix_stream_view(unix_file& owner) : _owner(owner) {}

            u16 family() const override {
                return AF_UNIX;
            }

            u32 socket_type() const override {
                return SOCK_STREAM;
            }

            long                 bind(const astia_sockaddr& address,
                                      const char*           canonical = nullptr) override;
            long                 listen(u32 backlog) override;
            core::task<accepted> accept(core::cltoken token = {}) override;
            long                 connect(const astia_sockaddr& address,
                                         const char*           canonical = nullptr) override;
            long                 shutdown(u32 how) override;
            long                 getsockopt(u32 level, u32 name, u32* value) override;

            core::task<i64>     sendmsg(span<const u8>                 data,
                                        const mm::shptr<unix_control>& control,
                                        core::cltoken                  token = {}) override;
            core::task<message> recvmsg(span<u8> data, core::cltoken token = {}) override;

            optional<readable&> as_readable() override;
            optional<writable&> as_writable() override;
            optional<pollable&> as_pollable() override;
            long                submit(io::request& request) override;

            private:
            unix_file& _owner;
        };

        class unix_file final : public file,
                                public readable,
                                public writable,
                                public pollable {
            public:
            enum class state : u8 { fresh, listener, connecting, connected, dgram };

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

            optional<dgram_sock&> as_dgram_sock() override {
                core::spinlock_guard guard(_lock);

                if (_state == state::dgram)
                    return _dgram_view;

                return {};
            }

            optional<stream_sock&> as_stream_sock() override {
                core::spinlock_guard guard(_lock);

                if (_state != state::dgram)
                    return _stream_view;

                return {};
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
                                   core::cltoken token, bool nonblock) override {
                return submit_writev(buffers, offset, token, nonblock);
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
                u32 read_begin = 0;
                {
                    core::spinlock_guard channel_guard(channel->lock);

                    if (channel->shut_rd[side])
                        return 0;

                    const u32 peer = socket_peer_side(side),
                              h    = astia::atomic_ref<u32>(channel->head[peer])
                                      .load(astia::memory_order::acquire),
                              t = astia::atomic_ref<u32>(channel->tail[peer])
                                      .load(astia::memory_order::relaxed);

                    if (h == t)
                        return 0;

                    count = h - t < n ? h - t : n;

                    u8* out = static_cast<u8*>(b);

                    for (u32 i = 0; i < count; ++i) {
                        out[i] = channel->data[peer][(t + i) &
                                                     (socket_stream_capacity_bytes - 1)];
                    }

                    astia::atomic_ref<u32>(channel->tail[peer])
                        .store(t + count, astia::memory_order::release);

                    read_begin = t;
                }
                {
                    core::spinlock_guard guard(_lock);

                    _last_read_begin = read_begin;
                    _last_read_end   = read_begin + count;
                }
                // Readiness may synchronously resume a parked operation.  Never
                // call into the AIO engine while the channel lock is held.
                sys::io_device_ready_context(this);
                return count;
            }

            long write_ready(const void* b, u32 n, u64) override {
                state                   kind;
                mm::shptr<channel>      channel;
                mm::shptr<unix_file>    peer_owner;
                mm::shptr<unix_control> control;
                u32                     side;
                {
                    core::spinlock_guard guard(_lock);

                    kind       = _state;
                    channel    = _ch;
                    peer_owner = _peer_owner;
                    control    = _send_control;
                    side       = _side;
                }
                if (kind == state::dgram) {
                    const long result = send_dgram(
                        astia::span<const u8>(static_cast<const u8*>(b), n), control);
                    if (result >= 0) {
                        core::spinlock_guard guard(_lock);

                        _send_control = {};
                    }

                    return result;
                }
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

                    u32 h = astia::atomic_ref<u32>(channel->head[side])
                                .load(astia::memory_order::relaxed),
                        t = astia::atomic_ref<u32>(channel->tail[side])
                                .load(astia::memory_order::acquire);
                    u32 free = socket_stream_capacity_bytes - (h - t);
                    if (!free)
                        return 0;

                    count = free < n ? free : n;

                    if (control) {
                        if (channel->control_count[side] >= socket_control_queue_capacity)
                            return 0;

                        const u32 index = channel->control_tail[side];

                        control->offset               = h;
                        channel->control[side][index] = control;
                        channel->control_tail[side] =
                            (index + 1) % socket_control_queue_capacity;

                        ++channel->control_count[side];
                    }

                    const u8* in = static_cast<const u8*>(b);

                    for (u32 i = 0; i < count; ++i) {
                        channel
                            ->data[side][(h + i) & (socket_stream_capacity_bytes - 1)] =
                            in[i];
                    }

                    astia::atomic_ref<u32>(channel->head[side])
                        .store(h + count, astia::memory_order::release);
                }
                if (control) {
                    core::spinlock_guard guard(_lock);

                    _send_control = {};
                }
                // As above, readiness callbacks must run after channel unlock.
                if (peer_owner)
                    sys::io_device_ready_context(peer_owner.get());
                return count;
            }

            long set_send_control(const mm::shptr<unix_control>& control) {
                core::spinlock_guard guard(_lock);

                _send_control = control;

                return 0;
            }

            mm::shptr<unix_control> take_recv_control() {
                state                   kind;
                mm::shptr<channel>      channel;
                mm::shptr<unix_control> control;

                u32 side;
                u32 begin;
                u32 end;

                {
                    core::spinlock_guard guard(_lock);

                    kind    = _state;
                    channel = _ch;
                    side    = _side;
                    begin   = _last_read_begin;
                    end     = _last_read_end;

                    if (kind == state::dgram) {
                        control            = _dgram_control_out;
                        _dgram_control_out = {};
                        _want_control      = false;

                        return control;
                    }
                }

                if (kind != state::connected || !channel)
                    return {};

                const u32 peer = socket_peer_side(side);

                core::spinlock_guard channel_guard(channel->lock);

                while (channel->control_count[peer]) {
                    const u32 index = channel->control_head[peer];

                    auto& slot = channel->control[peer][index];

                    const auto advance = [&] {
                        slot = {};
                        channel->control_head[peer] =
                            (index + 1) % socket_control_queue_capacity;

                        --channel->control_count[peer];
                    };

                    if (!slot) {
                        advance();

                        continue;
                    }

                    if (static_cast<i32>(slot->offset - begin) < 0) {
                        advance();

                        continue;
                    }

                    if (static_cast<i32>(slot->offset - end) < 0) {
                        control = slot;

                        advance();

                        return control;
                    }

                    break;
                }

                return {};
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

                    const u32 peer = socket_peer_side(side);

                    if (astia::atomic_ref<u32>(channel->head[peer])
                            .load(astia::memory_order::acquire) !=
                        astia::atomic_ref<u32>(channel->tail[peer])
                            .load(astia::memory_order::relaxed))
                        result |= POLLIN;

                    if (channel->control_count[peer])
                        result |= POLLIN;

                    if (channel->closed[side] || channel->shut_rd[side])
                        result |= POLLIN | POLLHUP;

                    if (channel->shut_wr[peer])
                        result |= POLLIN | POLLHUP;

                    if (channel->closed[peer])
                        result |= POLLERR;
                    else if (!channel->shut_wr[side] &&
                             socket_stream_capacity_bytes -
                                     (astia::atomic_ref<u32>(channel->head[side])
                                          .load(astia::memory_order::relaxed) -
                                      astia::atomic_ref<u32>(channel->tail[side])
                                          .load(astia::memory_order::acquire)) !=
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
                    if (r.direction() == io::direction::output) {
                        const long n = send_dgram(astia::span<const u8>(
                            static_cast<const u8*>(r.buffer()), r.length()));

                        // A full queue parks a blocking sender and fails a
                        // non-blocking one; 0 is only a queued empty datagram.
                        if (n == -EAGAIN && !r.nonblock()) {
                            r.stage(1);

                            return 0;
                        }

                        if (n == 0)
                            r.complete(true);

                        return n;
                    }

                    const long n = recv_dgram(
                        astia::span<u8>(static_cast<u8*>(r.buffer()), r.length()));

                    if (n == 0 && !r.complete())
                        r.stage(1);

                    return n;
                }
                long n           = r.direction() == io::direction::input
                                       ? read_ready(r.buffer(), r.length(), 0)
                                       : write_ready(r.buffer(), r.length(), 0);
                bool peer_closed = false;
                if (channel) {
                    core::spinlock_guard channel_guard(channel->lock);

                    const u32 peer = socket_peer_side(side);

                    peer_closed = channel->closed[peer] || channel->shut_wr[peer];
                }
                if (n == 0 && r.direction() == io::direction::input && peer_closed)
                    r.complete(true);
                else if (n == 0)
                    r.stage(1);
                return n;
            }

            void duplicate(u32) override {
                astia::atomic_ref<u32>(_opens).fetch_add(1u,
                                                         astia::memory_order::acq_rel);
            }

            void close(u32) override {
                if (astia::atomic_ref<u32>(_opens).fetch_sub(
                        1u, astia::memory_order::acq_rel) != 1)
                    return;

                cancel_accept_waiters();

                bool                 bound;
                char                 path[path_max]{};
                mm::shptr<unix_file> peer;
                mm::shptr<unix_file> target;
                mm::shptr<channel>   channel;
                u32                  side;
                {
                    core::spinlock_guard guard(_lock);

                    bound = _bound;

                    copy_path(path, _bound_path);

                    _bound              = false;
                    _closed             = true;
                    peer                = _peer_owner;
                    target              = _dgram_target_owner;
                    _peer_owner         = {};
                    _dgram_target_owner = {};
                    channel             = _ch;
                    side                = _side;
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

            void set_self(const mm::shptr<unix_file>& value) {
                _self = value;
            }

            void make_dgram() {
                core::spinlock_guard guard(_lock);

                _state = state::dgram;
            }

            long reserve_bind(const astia_sockaddr& a, const char* path) {
                core::spinlock_guard guard(_lock);

                if (_bound || (_state != state::fresh && _state != state::dgram))
                    return -EINVAL;

                _bound_addr = a;
                _bound      = true;

                copy_path(_bound_path, path);

                return 0;
            }

            void abort_bind() {
                core::spinlock_guard guard(_lock);

                _bound         = false;
                _bound_addr    = {};
                _bound_path[0] = '\0';
            }

            void commit_bind(const char* path) {
                core::spinlock_guard guard(_lock);

                copy_path(_bound_path, path);
            }

            astia_sockaddr bound_address() const {
                core::spinlock_guard guard(_lock);

                return _bound_addr;
            }

            /// The address this connection's peer bound, trimmed to the
            /// actual pathname.  An unnamed peer still reports AF_UNIX with
            /// the family-only length.
            astia_sockaddr peer_address() const {
                astia_sockaddr       out{};
                mm::shptr<unix_file> peer;

                {
                    core::spinlock_guard guard(_lock);

                    peer = _peer_owner;
                }

                if (!peer)
                    return out;

                const astia_sockaddr bound = peer->bound_address();

                out.family = AF_UNIX;

                if (bound.family != AF_UNIX || bound.len < 2) {
                    out.len = sizeof(u16);

                    return out;
                }

                u32 path = bound.len - 2;

                if (path > sizeof(out.data))
                    path = sizeof(out.data);

                for (u32 i = 0; i < path; ++i) {
                    out.data[i] = bound.data[i];

                    if (bound.data[i] == '\0') {
                        path = i + 1;

                        break;
                    }
                }

                out.len = 2 + path;

                return out;
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

            long canonical(const astia_sockaddr& a, const char* hint, char* out) {
                if (hint != nullptr) {
                    for (u32 i = 0; i < path_max; ++i) {
                        out[i] = hint[i];

                        if (out[i] == '\0')
                            return 0;
                    }

                    return -EINVAL;
                }

                if (!path(a, out))
                    return -EINVAL;

                auto* current = proc::current_process();
                if (!current)
                    return -ESRCH;

                return static_cast<long>(
                    current->resolve_path(out, out, path_max, false));
            }

            long bind_address(const astia_sockaddr& a, const char* hint) {
                char n[path_max];

                long rc = canonical(a, hint, n);
                if (rc < 0)
                    return rc;

                auto* current = proc::current_process();

                rc = reserve_bind(a, n);
                if (rc < 0)
                    return rc;

                auto self = mm::shptr<file>::borrowed(this);

                rc = register_unix_path(n, self, uid_t{current->euid()},
                                        gid_t{current->egid()});
                if (rc < 0) {
                    abort_bind();

                    return rc;
                }

                commit_bind(n);

                return 0;
            }

            long connect_address(const astia_sockaddr& a, const char* hint) {
                char n[path_max];

                long canonical_rc = canonical(a, hint, n);
                if (canonical_rc < 0)
                    return canonical_rc;

                if (kind() == state::dgram) {
                    mm::shptr<file> endpoint;

                    if (lookup_unix_path(n, endpoint) < 0)
                        return -ECONNREFUSED;

                    auto* target = static_cast<unix_file*>(endpoint.get());

                    if (!target || target->kind() != state::dgram)
                        return -ENOTSUP;

                    set_dgram_target(endpoint);

                    return 0;
                }

                long rc = reserve_connect();
                if (rc < 0)
                    return rc;

                mm::shptr<file> endpoint;

                if (lookup_unix_path(n, endpoint) < 0) {
                    abort_connect();

                    return -ECONNREFUSED;
                }

                auto* listener = static_cast<unix_file*>(endpoint.get());

                if (!listener || listener->kind() != state::listener) {
                    abort_connect();

                    return -ECONNREFUSED;
                }

                auto ch = mm::share<channel>();
                auto s  = mm::share<unix_file>();

                if (!ch || !s) {
                    abort_connect();

                    return -ENOMEM;
                }

                auto c_owner = _self.lock();
                if (!c_owner) {
                    abort_connect();

                    return -EBADF;
                }

                attach(ch, socket_side_first, s);
                s->attach(ch, socket_side_second, c_owner);

                auto so = mm::share<open_file>(s, 0);

                if (!so || !listener->enqueue(so)) {
                    abort_connect();

                    return -ECONNREFUSED;
                }

                return 0;
            }

            long sendto_address(const astia_sockaddr& a) {
                if (kind() != state::dgram)
                    return -EOPNOTSUPP;

                char n[path_max];

                long rc = canonical(a, nullptr, n);
                if (rc < 0)
                    return rc;

                mm::shptr<file> endpoint;

                if (lookup_unix_path(n, endpoint) < 0)
                    return -ECONNREFUSED;

                auto* target = static_cast<unix_file*>(endpoint.get());

                if (!target || target->kind() != state::dgram)
                    return -ECONNREFUSED;

                set_dgram_target(endpoint);

                return 0;
            }

            long getsockopt(u32 level, u32 name, u32* value) {
                if (value == nullptr)
                    return -ENOTSOCK;

                const state current = kind();

                if (current != state::connected && current != state::dgram)
                    return -ENOTSOCK;

                if (level != SOL_SOCKET || name != SO_SNDBUF)
                    return -ENOPROTOOPT;

                *value = socket_stream_capacity_bytes;

                return 0;
            }

            core::task<i64> sendto_async(span<const u8>        data,
                                         const astia_sockaddr& destination,
                                         core::cltoken token, bool nonblock = false) {
                if (kind() != state::dgram)
                    co_return -EOPNOTSUPP;

                const long queued = sendto_address(destination);
                if (queued < 0)
                    co_return queued;

                buffer     entry{const_cast<u8*>(data.data()), data.size()};
                const auto result = co_await submit_writev(span<const buffer>(&entry, 1),
                                                           0, token, nonblock);

                co_return result ? result.value : error_errno(result.error);
            }

            core::task<message> recvfrom_async(span<u8> data, core::cltoken token) {
                message result{};

                if (kind() != state::dgram) {
                    result.length = -EOPNOTSUPP;

                    co_return result;
                }

                set_dgram_address();

                buffer     entry{data.data(), data.size()};
                const auto received =
                    co_await submit_readv(span<const buffer>(&entry, 1), 0, token);

                if (!received) {
                    result.length = error_errno(received.error);

                    co_return result;
                }

                result.length = received.value;

                if (result.length >= 0)
                    take_dgram_address(result.source);

                co_return result;
            }

            core::task<i64> sendmsg_async(span<const u8>                 data,
                                          const astia_sockaddr*          destination,
                                          const mm::shptr<unix_control>& control,
                                          core::cltoken token, bool nonblock = false) {
                if (destination != nullptr) {
                    const long queued = sendto_address(*destination);
                    if (queued < 0)
                        co_return queued;
                }

                set_send_control(control);

                buffer     entry{const_cast<u8*>(data.data()), data.size()};
                const auto result = co_await submit_writev(span<const buffer>(&entry, 1),
                                                           0, token, nonblock);

                set_send_control({});

                co_return result ? result.value : error_errno(result.error);
            }

            core::task<message> recvmsg_async(span<u8> data, core::cltoken token,
                                              bool with_control) {
                message     result{};
                const state current = kind();

                if (current == state::dgram) {
                    set_dgram_address();

                    core::spinlock_guard guard(_lock);

                    _want_control = with_control;
                }
                else if (current != state::connected) {
                    result.length = -ENOTCONN;

                    co_return result;
                }

                buffer     entry{data.data(), data.size()};
                const auto received =
                    co_await submit_readv(span<const buffer>(&entry, 1), 0, token);

                if (!received) {
                    result.length = error_errno(received.error);

                    co_return result;
                }

                result.length = received.value;

                if (result.length >= 0) {
                    if (current == state::dgram)
                        take_dgram_address(result.source);

                    if (with_control)
                        result.control = take_recv_control();
                }

                co_return result;
            }

            long send_dgram(astia::span<const u8>   payload,
                            mm::shptr<unix_control> control = {}) {
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
                        return -EAGAIN;

                    datagram& m = target->_dgrams[target->_dgram_tail];

                    m.len     = static_cast<u32>(payload.size());
                    m.from    = from;
                    m.control = control;

                    for (u32 i = 0; i < m.len; ++i) {
                        m.data[i] = payload.data()[i];
                    }

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

                u32 count;

                {
                    core::spinlock_guard guard(_lock);

                    if (!_dgram_count)
                        return _shut_rd ? 0 : 0;

                    datagram& m = _dgrams[_dgram_head];

                    count =
                        output.size() < m.len ? static_cast<u32>(output.size()) : m.len;

                    for (u32 i = 0; i < count; ++i) {
                        output.data()[i] = m.data[i];
                    }

                    if (_dgram_out_pending) {
                        _dgram_out_value   = m.from;
                        _dgram_out_pending = false;
                    }

                    mm::shptr<unix_control> control = m.control;

                    m.control = {};
                    _dgram_control_out =
                        _want_control ? control : mm::shptr<unix_control>{};
                    _want_control = false;

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

            long reserve_connect() {
                core::spinlock_guard guard(_lock);

                if (_state == state::connecting)
                    return -EALREADY;

                if (_state == state::connected ||
                    (_state == state::dgram && _dgram_target_owner))
                    return -EISCONN;

                if (_state == state::listener)
                    return -EINVAL;

                _connect_resume = _state;
                _state          = state::connecting;

                return 0;
            }

            bool connect_is_datagram() const {
                core::spinlock_guard guard(_lock);

                return _connect_resume == state::dgram;
            }

            void attach_dgram(const mm::shptr<file>& endpoint) {
                core::spinlock_guard guard(_lock);

                _dgram_target_owner = endpoint;
                _state              = state::dgram;
            }

            void abort_connect() {
                core::spinlock_guard guard(_lock);

                _ch         = {};
                _peer_owner = {};
                _state      = _connect_resume;
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

                u32 side;

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

                wake_accept_one();
                sys::io_device_ready_context(this);

                return true;
            }

            core::task<accepted> accept_async(core::cltoken token) {
                accepted result{};

                if (kind() != state::listener) {
                    result.fd = -EINVAL;

                    co_return result;
                }

                while (true) {
                    if (token.canceled()) {
                        result.fd = -ECANCELED;

                        co_return result;
                    }

                    auto*                current = proc::current_process();
                    mm::shptr<open_file> pending;

                    u32 installed_fd = 0;

                    {
                        core::spinlock_guard guard(_lock);

                        if (_queue_count != 0) {
                            if (!current) {
                                result.fd = -ESRCH;

                                co_return result;
                            }

                            pending = _queue[_queue_head];

                            const auto installed = current->allocate_descriptor(pending);

                            if (!installed) {
                                result.fd = -EMFILE;

                                co_return result;
                            }

                            _queue[_queue_head] = {};
                            _queue_head = (_queue_head + 1) % socket_backlog_capacity;

                            --_queue_count;

                            installed_fd = installed.value.value;
                        }
                    }

                    if (pending) {
                        result.fd = static_cast<i64>(installed_fd);

                        if (auto* accepted =
                                static_cast<unix_file*>(pending->parent().get()))
                            result.peer = accepted->peer_address();

                        co_return result;
                    }

                    accept_waiter* node = acquire_accept_waiter();
                    if (!node) {
                        result.fd = -EAGAIN;

                        co_return result;
                    }

                    if (!arm_accept_waiter(node)) {
                        release_accept_waiter(node);

                        continue;
                    }

                    accept_scope scope(*this, node);

                    const auto woke = co_await node->done;

                    if (!woke.has_value) {
                        result.fd = -ECANCELED;

                        co_return result;
                    }
                }
            }

            accept_waiter* acquire_accept_waiter() {
                core::spinlock_guard guard(_lock);

                for (accept_waiter& value : _accept_waiters) {
                    if (value.refs != 0)
                        continue;

                    new (&value.done) core::completion<u8>();

                    value.next  = nullptr;
                    value.refs  = 1;
                    value.state = accept_waiter_state::free;

                    return &value;
                }

                return nullptr;
            }

            bool arm_accept_waiter(accept_waiter* value) {
                core::spinlock_guard guard(_lock);

                if (_queue_count != 0)
                    return false;

                value->next     = _accept_waiting;
                _accept_waiting = value;
                value->state    = accept_waiter_state::waiting;

                return true;
            }

            void release_accept_waiter(accept_waiter* value) {
                if (value == nullptr)
                    return;

                core::spinlock_guard guard(_lock);

                if (value->refs == 0)
                    return;

                --value->refs;

                if (value->refs == 0) {
                    value->next  = nullptr;
                    value->state = accept_waiter_state::free;
                }
            }

            void wake_accept_one() {
                accept_waiter* value = nullptr;

                {
                    core::spinlock_guard guard(_lock);

                    if (_accept_waiting == nullptr)
                        return;

                    value           = _accept_waiting;
                    _accept_waiting = value->next;

                    value->next  = nullptr;
                    value->state = accept_waiter_state::delivering;

                    ++value->refs;
                }

                value->done.complete_deferred(1);
                release_accept_waiter(value);
            }

            void cancel_accept_waiters() {
                accept_waiter* value = nullptr;

                {
                    core::spinlock_guard guard(_lock);

                    value           = _accept_waiting;
                    _accept_waiting = nullptr;

                    for (accept_waiter* cursor = value; cursor != nullptr;
                         cursor                = cursor->next) {
                        cursor->state = accept_waiter_state::delivering;

                        ++cursor->refs;
                    }
                }

                while (value != nullptr) {
                    accept_waiter* next = value->next;

                    value->next = nullptr;

                    value->done.fail_deferred(error_t::ecanceled);
                    release_accept_waiter(value);

                    value = next;
                }
            }

            private:
            mm::shptr<channel>      _ch;
            u32                     _side{};
            mm::shptr<unix_file>    _peer_owner;
            mutable core::spinlock  _lock;
            state                   _state{state::fresh};
            state                   _connect_resume{state::fresh};
            bool                    _closed{};
            u32                     _backlog{};
            bool                    _bound{};
            char                    _bound_path[path_max]{};
            mm::shptr<unix_file>    _dgram_target_owner;
            astia_sockaddr          _dgram_out_value{};
            bool                    _dgram_out_pending{};
            mm::shptr<unix_control> _send_control;
            mm::shptr<unix_control> _dgram_control_out;
            bool                    _want_control{};
            u32                     _last_read_begin{};
            u32                     _last_read_end{};
            datagram                _dgrams[socket_datagram_queue_capacity]{};
            u32                     _dgram_head{}, _dgram_tail{}, _dgram_count{};
            bool                    _shut_rd{};
            astia_sockaddr          _bound_addr{};
            mm::shptr<open_file>    _queue[socket_backlog_capacity]{};
            u32                     _queue_head{}, _queue_tail{}, _queue_count{};
            accept_waiter           _accept_waiters[accept_waiter_limit]{};
            accept_waiter*          _accept_waiting = nullptr;
            u32                     _opens{1};
            mm::shweak<unix_file>   _self;

            unix_dgram_view  _dgram_view{*this};
            unix_stream_view _stream_view{*this};
        };

        optional<readable&> unix_dgram_view::as_readable() {
            return _owner.as_readable();
        }

        optional<writable&> unix_dgram_view::as_writable() {
            return _owner.as_writable();
        }

        optional<pollable&> unix_dgram_view::as_pollable() {
            return _owner.as_pollable();
        }

        long unix_dgram_view::submit(io::request& request) {
            return _owner.submit(request);
        }

        optional<readable&> unix_stream_view::as_readable() {
            return _owner.as_readable();
        }

        optional<writable&> unix_stream_view::as_writable() {
            return _owner.as_writable();
        }

        optional<pollable&> unix_stream_view::as_pollable() {
            return _owner.as_pollable();
        }

        long unix_stream_view::submit(io::request& request) {
            return _owner.submit(request);
        }

        accept_scope::accept_scope(unix_file& owner, accept_waiter* value)
            : _owner(&owner), _value(value) {}

        accept_scope::~accept_scope() {
            if (_owner != nullptr)
                _owner->release_accept_waiter(_value);
        }

        long unix_dgram_view::bind(const astia_sockaddr& address, const char* canonical) {
            return _owner.bind_address(address, canonical);
        }

        long unix_dgram_view::connect(const astia_sockaddr& address,
                                      const char*           canonical) {
            return _owner.connect_address(address, canonical);
        }

        long unix_dgram_view::getsockopt(u32 level, u32 name, u32* value) {
            return _owner.getsockopt(level, name, value);
        }

        core::task<i64> unix_dgram_view::sendto(span<const u8>        data,
                                                const astia_sockaddr& destination,
                                                core::cltoken token, bool nonblock) {
            return _owner.sendto_async(data, destination, token, nonblock);
        }

        core::task<message> unix_dgram_view::recvfrom(span<u8>      data,
                                                      core::cltoken token) {
            return _owner.recvfrom_async(data, token);
        }

        core::task<i64> unix_dgram_view::sendmsg(span<const u8>        data,
                                                 const astia_sockaddr* destination,
                                                 const mm::shptr<unix_control>& control,
                                                 core::cltoken token, bool nonblock) {
            return _owner.sendmsg_async(data, destination, control, token, nonblock);
        }

        core::task<message> unix_dgram_view::recvmsg(span<u8> data, core::cltoken token) {
            return _owner.recvmsg_async(data, token, true);
        }

        long unix_stream_view::bind(const astia_sockaddr& address,
                                    const char*           canonical) {
            return _owner.bind_address(address, canonical);
        }

        long unix_stream_view::listen(u32 backlog) {
            if (_owner.kind() != unix_file::state::fresh)
                return -EINVAL;

            _owner.make_listener(backlog > socket_backlog_capacity
                                     ? socket_backlog_capacity
                                 : backlog ? backlog
                                           : 1);

            return 0;
        }

        core::task<accepted> unix_stream_view::accept(core::cltoken token) {
            return _owner.accept_async(token);
        }

        long unix_stream_view::connect(const astia_sockaddr& address,
                                       const char*           canonical) {
            return _owner.connect_address(address, canonical);
        }

        long unix_stream_view::shutdown(u32 how) {
            return _owner.shutdown(how);
        }

        long unix_stream_view::getsockopt(u32 level, u32 name, u32* value) {
            return _owner.getsockopt(level, name, value);
        }

        core::task<i64> unix_stream_view::sendmsg(span<const u8>                 data,
                                                  const mm::shptr<unix_control>& control,
                                                  core::cltoken                  token) {
            return _owner.sendmsg_async(data, nullptr, control, token);
        }

        core::task<message> unix_stream_view::recvmsg(span<u8>      data,
                                                      core::cltoken token) {
            return _owner.recvmsg_async(data, token, false);
        }
    }

    long create_unix_socket(mm::shptr<open_file>& out, u32 type) {
        if (type != SOCK_STREAM && type != SOCK_DGRAM)
            return -EAFNOSUPPORT;

        auto f = mm::share<unix_file>();
        if (!f)
            return -ENOMEM;

        f->set_self(f);

        if (type == SOCK_DGRAM)
            f->make_dgram();

        out = mm::share<open_file>(f, 0);

        return out ? 0 : -ENOMEM;
    }

    long unix_socketpair(mm::shptr<open_file>& left, mm::shptr<open_file>& right,
                         u32 type) {
        if (type != SOCK_STREAM && type != SOCK_DGRAM)
            return -EPROTOTYPE;

        auto a = mm::share<unix_file>();
        auto b = mm::share<unix_file>();

        if (!a || !b)
            return -ENOMEM;

        a->set_self(a);
        b->set_self(b);

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
}
