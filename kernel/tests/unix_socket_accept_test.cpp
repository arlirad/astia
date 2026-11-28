#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/fs/socket.hpp>
#include <astia/strings.hpp>

#include "aio_registry_test_support.hpp"
#include "support/aio_test_file.hpp"
#include "support/test.hpp"
#include <io/aio/registry.hpp>
#include <io/aio/ring.hpp>
#include <ipc/unix_socket.hpp>
#include <proc/proc.hpp>

#include <atomic>
#include <thread>

using namespace astia;

namespace {
    template <typename T>
    T drive_task(core::task<T> task) {
        while (!task.done()) {
            ASTIA_CHECK(task.resume());
        }

        ASTIA_CHECK(!task.failed());
        ASTIA_CHECK(task.value() != nullptr);

        return *task.value();
    }
} // namespace

namespace astia::fs {
    namespace {
        struct path_registration {
            char            path[path_max]{};
            mm::shptr<file> endpoint;
        };

        constexpr u32 registration_capacity = 8;

        path_registration registrations[registration_capacity];
        u32               registration_count;

        std::atomic<bool> lookup_blocked;
        std::atomic<bool> lookup_entered;
    } // namespace

    mm::shptr<file> unix_lookup_endpoint;

    long register_unix_path(const char* path, const mm::shptr<file>& endpoint, uid_t,
                            gid_t, astia::span<char>) {
        if (registration_count == registration_capacity)
            return -ENFILE;

        auto& registration = registrations[registration_count++];

        for (u32 i = 0; i < path_max; ++i) {
            registration.path[i] = path[i];

            if (!path[i])
                break;
        }

        registration.endpoint = endpoint;

        return 0;
    }

    long lookup_unix_path(const char*, mm::shptr<file>& endpoint) {
        while (lookup_blocked.load()) {
            lookup_entered.store(true);
            std::this_thread::yield();
        }

        if (!unix_lookup_endpoint)
            return -ENOENT;

        endpoint = unix_lookup_endpoint;

        return 0;
    }

    void unregister_unix_path(const char*, file* endpoint) {
        for (u32 i = 0; i < registration_count; ++i) {
            if (registrations[i].endpoint.get() != endpoint)
                continue;

            for (u32 j = i + 1; j < registration_count; ++j) {
                registrations[j - 1] = registrations[j];
            }

            registrations[--registration_count] = {};

            break;
        }
    }

    void reset_unix_registrations() {
        for (auto& registration : registrations) {
            registration = {};
        }

        registration_count   = 0;
        unix_lookup_endpoint = {};

        lookup_blocked.store(false);
        lookup_entered.store(false);
    }

    u32 unix_registration_count() {
        return registration_count;
    }

    const char* unix_registration_path(u32 index) {
        return registrations[index].path;
    }
} // namespace astia::fs

namespace {
    namespace fs  = astia::fs;
    namespace aio = astia::io::aio;

    void set_unix_lookup_endpoint(const astia::mm::shptr<fs::file>& endpoint) {
        fs::unix_lookup_endpoint = endpoint;
    }

    astia_sockaddr socket_address(const char* path = "/") {
        astia_sockaddr address{};

        address.family = AF_UNIX;

        u32 length = 0;

        while (path[length]) {
            ++length;
        }

        address.len = length + 2;

        for (u32 i = 0; i < length; ++i) {
            address.data[i] = static_cast<u8>(path[i]);
        }

        return address;
    }

    astia_sockaddr path_address(const char* path) {
        astia_sockaddr address{};

        u32 length = 0;

        while (path[length] != '\0' && length < sizeof(address.data) - 1) {
            ++length;
        }

        address.family = AF_UNIX;
        address.len    = 2 + length + 1;

        for (u32 i = 0; i <= length; ++i) {
            address.data[i] = static_cast<u8>(path[i]);
        }

        return address;
    }

    struct accept_pair {
        mm::shptr<fs::open_file> listener;
        mm::shptr<fs::open_file> client;
        fs::stream_sock*         listener_ops = nullptr;
        fs::stream_sock*         client_ops   = nullptr;
        i32                      listener_fd  = -1;
    };

    bool prepare_accept(accept_pair& pair, const char* client_path) {
        using namespace astia::io::aio::test_support;

        if (fs::create_unix_socket(pair.listener, SOCK_STREAM) != 0 ||
            fs::create_unix_socket(pair.client, SOCK_STREAM) != 0)
            return false;

        if (!pair.listener || !pair.client)
            return false;

        auto listener_socket = pair.listener->parent()->as_stream_sock();
        auto client_socket   = pair.client->parent()->as_stream_sock();

        pair.listener_ops = listener_socket ? &*listener_socket : nullptr;
        pair.client_ops   = client_socket ? &*client_socket : nullptr;

        if (!pair.listener_ops || !pair.client_ops)
            return false;

        set_unix_lookup_endpoint(pair.listener->parent());

        if (client_path != nullptr &&
            pair.client_ops->bind(path_address(client_path)) != 0)
            return false;

        if (pair.listener_ops->listen(1) != 0)
            return false;

        pair.listener_fd = install_descriptor(pair.listener);

        if (pair.listener_fd < 0)
            return false;

        return pair.client_ops->connect(socket_address()) == 0;
    }

    i32 dispatch_accept(accept_pair& pair, astia_sockaddr* peer,
                        astia::io::aio::test_support::cq_capture& capture,
                        aio::ring::ring_slot& slot, u32 index) {
        astia_sqe sqe{};

        sqe.op      = OP_ACCEPT;
        sqe.fd      = pair.listener_fd;
        sqe.address = reinterpret_cast<u64>(peer);

        if (!aio::registry::dispatch(slot, &capture.ring, sqe))
            return -EINVAL;

        return capture.result(index);
    }
} // namespace

ASTIA_TEST(unix_socket_accept_retains_queued_connection_when_fds_exhausted) {
    using namespace astia::io::aio::test_support;

    reset_environment();
    install_current_process();
    fs::reset_unix_registrations();

    mm::shptr<fs::open_file> listener;
    mm::shptr<fs::open_file> client;

    ASTIA_CHECK(fs::create_unix_socket(listener, SOCK_STREAM) == 0);
    ASTIA_CHECK(fs::create_unix_socket(client, SOCK_STREAM) == 0);

    if (!listener || !client)
        return;

    set_unix_lookup_endpoint(listener->parent());

    auto listener_ops = listener->parent()->as_stream_sock();
    auto client_ops   = client->parent()->as_stream_sock();

    ASTIA_CHECK(listener_ops.has_value);
    ASTIA_CHECK(client_ops.has_value);

    if (!listener_ops || !client_ops)
        return;

    ASTIA_CHECK(listener_ops->listen(1) == 0);

    auto filler = aio_test::make_open_file(new aio_test::fake_file);

    while (install_descriptor(filler) >= 0) {
    }

    const astia_sockaddr address = socket_address();

    ASTIA_CHECK(client_ops->connect(address) == 0);
    ASTIA_CHECK(listener->poll(POLLIN) == POLLIN);

    ASTIA_CHECK(drive_task(listener_ops->accept()).fd == -EMFILE);
    ASTIA_CHECK(listener->poll(POLLIN) == POLLIN);

    clear_descriptor(3);

    const fs::accepted accepted = drive_task(listener_ops->accept());

    ASTIA_CHECK(accepted.fd == 3);
    ASTIA_CHECK(descriptor_present(3));

    const auto server = proc::current_process()->descriptor(fd_t{3});

    ASTIA_CHECK(server);

    if (server) {
        char sent = 'x';

        io::request write_request(&sent, 1, 0, 0, io::direction::output);

        char received = 0;

        io::request read_request(&received, 1, 0, 0, io::direction::input);

        ASTIA_CHECK(server->submit(write_request) == 1);
        ASTIA_CHECK(client->submit(read_request) == 1);
        ASTIA_CHECK(received == 'x');
    }
}

ASTIA_TEST(unix_socket_second_bind_is_rejected_without_replacing_first) {
    using namespace astia::io::aio::test_support;

    reset_environment();
    install_current_process();
    fs::reset_unix_registrations();

    mm::shptr<fs::open_file> socket;

    ASTIA_CHECK(fs::create_unix_socket(socket, SOCK_STREAM) == 0);

    if (!socket)
        return;

    auto ops = socket->parent()->as_stream_sock();

    ASTIA_CHECK(ops.has_value);

    if (!ops)
        return;

    ASTIA_CHECK(ops->bind(socket_address("/tmp/first")) == 0);
    ASTIA_CHECK(fs::unix_registration_count() == 1);
    ASTIA_CHECK(astia::string_equal(fs::unix_registration_path(0), "/tmp/first"));

    ASTIA_CHECK(ops->bind(socket_address("/tmp/second")) == -EINVAL);
    ASTIA_CHECK(fs::unix_registration_count() == 1);
    ASTIA_CHECK(astia::string_equal(fs::unix_registration_path(0), "/tmp/first"));
}

ASTIA_TEST(unix_socket_rejected_second_bind_keeps_first_binding_until_close) {
    using namespace astia::io::aio::test_support;

    reset_environment();
    install_current_process();
    fs::reset_unix_registrations();

    mm::shptr<fs::open_file> socket;

    ASTIA_CHECK(fs::create_unix_socket(socket, SOCK_STREAM) == 0);

    if (!socket)
        return;

    auto ops = socket->parent()->as_stream_sock();

    ASTIA_CHECK(ops.has_value);

    if (!ops)
        return;

    ASTIA_CHECK(ops->bind(socket_address("/tmp/keep")) == 0);
    ASTIA_CHECK(ops->bind(socket_address("/tmp/discard")) == -EINVAL);
    ASTIA_CHECK(fs::unix_registration_count() == 1);
    ASTIA_CHECK(astia::string_equal(fs::unix_registration_path(0), "/tmp/keep"));

    socket = {};

    ASTIA_CHECK(fs::unix_registration_count() == 0);
}

ASTIA_TEST(unix_socket_second_connect_is_rejected_with_eisconn) {
    using namespace astia::io::aio::test_support;

    reset_environment();
    install_current_process();
    fs::reset_unix_registrations();

    mm::shptr<fs::open_file> listener;
    mm::shptr<fs::open_file> client;

    ASTIA_CHECK(fs::create_unix_socket(listener, SOCK_STREAM) == 0);
    ASTIA_CHECK(fs::create_unix_socket(client, SOCK_STREAM) == 0);

    if (!listener || !client)
        return;

    set_unix_lookup_endpoint(listener->parent());

    auto listener_ops = listener->parent()->as_stream_sock();
    auto client_ops   = client->parent()->as_stream_sock();

    ASTIA_CHECK(listener_ops.has_value);
    ASTIA_CHECK(client_ops.has_value);

    if (!listener_ops || !client_ops)
        return;

    ASTIA_CHECK(listener_ops->listen(1) == 0);

    const astia_sockaddr address = socket_address("/s");

    ASTIA_CHECK(client_ops->connect(address) == 0);
    ASTIA_CHECK(client_ops->connect(address) == -EISCONN);
    ASTIA_CHECK(listener->poll(POLLIN) == POLLIN);

    const fs::accepted accepted = drive_task(listener_ops->accept());

    ASTIA_CHECK(accepted.fd >= 0);

    if (accepted.fd >= 0)
        clear_descriptor(static_cast<i32>(accepted.fd));
}

ASTIA_TEST(unix_socket_connect_in_progress_is_rejected_with_ealready) {
    using namespace astia::io::aio::test_support;

    reset_environment();
    install_current_process();
    fs::reset_unix_registrations();

    mm::shptr<fs::open_file> listener;
    mm::shptr<fs::open_file> client;

    ASTIA_CHECK(fs::create_unix_socket(listener, SOCK_STREAM) == 0);
    ASTIA_CHECK(fs::create_unix_socket(client, SOCK_STREAM) == 0);

    if (!listener || !client)
        return;

    set_unix_lookup_endpoint(listener->parent());

    auto listener_ops = listener->parent()->as_stream_sock();
    auto client_ops   = client->parent()->as_stream_sock();

    ASTIA_CHECK(listener_ops.has_value);
    ASTIA_CHECK(client_ops.has_value);

    if (!listener_ops || !client_ops)
        return;

    ASTIA_CHECK(listener_ops->listen(1) == 0);

    const astia_sockaddr address = socket_address("/s");

    fs::lookup_blocked.store(true);

    std::atomic<long> first_result{-1};

    std::thread first([&] { first_result.store(client_ops->connect(address)); });

    while (!fs::lookup_entered.load()) {
        std::this_thread::yield();
    }

    ASTIA_CHECK(client_ops->connect(address) == -EALREADY);

    fs::lookup_blocked.store(false);
    first.join();

    ASTIA_CHECK(first_result.load() == 0);
    ASTIA_CHECK(listener->poll(POLLIN) == POLLIN);

    ASTIA_CHECK(drive_task(listener_ops->accept()).fd >= 0);
}

ASTIA_TEST(unix_socket_accept_reports_bound_peer_address) {
    using namespace astia::io::aio::test_support;

    reset_environment();
    install_current_process();

    fs::reset_unix_registrations();

    accept_pair pair;

    ASTIA_CHECK(prepare_accept(pair, "/accepted"));

    if (pair.listener_fd < 0)
        return;

    const astia_sockaddr expected = path_address("/accepted");

    astia_sockaddr       peer{};
    cq_capture           capture;
    aio::ring::ring_slot slot{};

    const i32 accepted = dispatch_accept(pair, &peer, capture, slot, 0);

    ASTIA_CHECK(accepted >= 4);
    ASTIA_CHECK(descriptor_present(accepted));
    ASTIA_CHECK(peer.family == AF_UNIX);
    ASTIA_CHECK(peer.len == expected.len);
    ASTIA_CHECK(__builtin_memcmp(peer.data, expected.data, expected.len - 2) == 0);

    release_parked(slot);
}

ASTIA_TEST(unix_socket_accept_reports_unnamed_peer) {
    using namespace astia::io::aio::test_support;

    reset_environment();
    install_current_process();

    fs::reset_unix_registrations();

    accept_pair pair;

    ASTIA_CHECK(prepare_accept(pair, nullptr));

    if (pair.listener_fd < 0)
        return;

    astia_sockaddr       peer{};
    cq_capture           capture;
    aio::ring::ring_slot slot{};

    const i32 accepted = dispatch_accept(pair, &peer, capture, slot, 0);

    ASTIA_CHECK(accepted >= 4);
    ASTIA_CHECK(peer.family == AF_UNIX);
    ASTIA_CHECK(peer.len == 2);

    release_parked(slot);
}

ASTIA_TEST(unix_socket_accept_without_address_succeeds) {
    using namespace astia::io::aio::test_support;

    reset_environment();
    install_current_process();

    fs::reset_unix_registrations();

    accept_pair pair;

    ASTIA_CHECK(prepare_accept(pair, "/accepted"));

    if (pair.listener_fd < 0)
        return;

    cq_capture           capture;
    aio::ring::ring_slot slot{};

    const i32 accepted = dispatch_accept(pair, nullptr, capture, slot, 0);

    ASTIA_CHECK(accepted >= 4);
    ASTIA_CHECK(descriptor_present(accepted));

    release_parked(slot);
}

ASTIA_TEST(unix_socket_accept_rejects_unmapped_address_without_consuming) {
    using namespace astia::io::aio::test_support;

    reset_environment();
    install_current_process();

    fs::reset_unix_registrations();

    accept_pair pair;

    ASTIA_CHECK(prepare_accept(pair, "/accepted"));

    if (pair.listener_fd < 0)
        return;

    astia_sockaddr       peer{};
    cq_capture           capture;
    aio::ring::ring_slot slot{};

    set_memory_allowed(false);

    ASTIA_CHECK(dispatch_accept(pair, &peer, capture, slot, 0) == -EFAULT);

    set_memory_allowed(true);

    ASTIA_CHECK(pair.listener->poll(POLLIN) == POLLIN);
    ASTIA_CHECK(dispatch_accept(pair, &peer, capture, slot, 1) >= 4);

    release_parked(slot);
}

ASTIA_TEST(unix_dgram_sendto_recvfrom_roundtrip) {
    using namespace astia::io::aio::test_support;

    reset_environment();
    install_current_process();
    fs::reset_unix_registrations();

    mm::shptr<fs::open_file> sender;
    mm::shptr<fs::open_file> receiver;

    ASTIA_CHECK(fs::create_unix_socket(sender, SOCK_DGRAM) == 0);
    ASTIA_CHECK(fs::create_unix_socket(receiver, SOCK_DGRAM) == 0);

    if (!sender || !receiver)
        return;

    auto sender_sock   = sender->parent()->as_dgram_sock();
    auto receiver_sock = receiver->parent()->as_dgram_sock();

    ASTIA_CHECK(sender_sock.has_value);
    ASTIA_CHECK(receiver_sock.has_value);

    if (!sender_sock || !receiver_sock)
        return;

    ASTIA_CHECK(sender_sock->bind(socket_address("/tmp/msg-a")) == 0);
    ASTIA_CHECK(receiver_sock->bind(socket_address("/tmp/msg-b")) == 0);

    set_unix_lookup_endpoint(receiver->parent());

    const u8 payload[] = {'p', 'i', 'n', 'g'};

    const i64 sent = drive_task(sender_sock->sendto(
        astia::span<const u8>(payload, sizeof payload), socket_address("/tmp/msg-b")));

    ASTIA_CHECK(sent == 4);

    u8 output[8]{};

    const fs::message message =
        drive_task(receiver_sock->recvfrom(astia::span<u8>(output, sizeof output)));

    ASTIA_CHECK(message.length == 4);
    ASTIA_CHECK(output[0] == 'p' && output[1] == 'i' && output[2] == 'n' &&
                output[3] == 'g');
    ASTIA_CHECK(message.source.family == AF_UNIX);
}

ASTIA_TEST(unix_stream_sendmsg_recvmsg_roundtrip) {
    using namespace astia::io::aio::test_support;

    reset_environment();
    install_current_process();

    mm::shptr<fs::open_file> left;
    mm::shptr<fs::open_file> right;

    ASTIA_CHECK(fs::unix_socketpair(left, right, SOCK_STREAM) == 0);

    if (!left || !right)
        return;

    auto left_sock  = left->parent()->as_stream_sock();
    auto right_sock = right->parent()->as_stream_sock();

    ASTIA_CHECK(left_sock.has_value);
    ASTIA_CHECK(right_sock.has_value);

    if (!left_sock || !right_sock)
        return;

    const u8 payload[] = {'s', 't', 'r', 'e', 'a', 'm'};

    const i64 sent = drive_task(
        left_sock->sendmsg(astia::span<const u8>(payload, sizeof payload), {}));

    ASTIA_CHECK(sent == 6);

    u8 output[8]{};

    const fs::message message =
        drive_task(right_sock->recvmsg(astia::span<u8>(output, sizeof output)));

    ASTIA_CHECK(message.length == 6);
    ASTIA_CHECK(output[0] == 's' && output[5] == 'm');
}
