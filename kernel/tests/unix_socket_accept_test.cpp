#include <astia/api/aio.h>
#include <astia/api/errno.h>
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

        char lookup_last_path[path_max];
        char lookup_required_path[path_max];
        bool lookup_requires_path;
    } // namespace

    mm::shptr<file> unix_lookup_endpoint;

    long register_unix_path(const char* path, const mm::shptr<file>& endpoint, uid_t,
                            gid_t) {
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

    long lookup_unix_path(const char* path, mm::shptr<file>& endpoint) {
        while (lookup_blocked.load()) {
            lookup_entered.store(true);
            std::this_thread::yield();
        }

        for (u32 i = 0; i < path_max; ++i) {
            lookup_last_path[i] = path[i];

            if (!path[i])
                break;
        }

        if (lookup_requires_path && !astia::string_equal(path, lookup_required_path))
            return -ENOENT;

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

        lookup_last_path[0]     = '\0';
        lookup_required_path[0] = '\0';
        lookup_requires_path    = false;

        lookup_blocked.store(false);
        lookup_entered.store(false);
    }

    const char* unix_lookup_path() {
        return lookup_last_path;
    }

    void require_unix_lookup_path(const char* path) {
        lookup_requires_path = path != nullptr;

        for (u32 i = 0; i < path_max; ++i) {
            lookup_required_path[i] = path == nullptr ? '\0' : path[i];

            if (path != nullptr && !path[i])
                break;
        }
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

    void set_jail_directory() {
        astia::io::aio::test_support::set_node_mode(
            "/jail", fs::file_mode(fs::file_type::directory, fs::file_permissions(0755)));
    }

    astia_sqe socket_sqe(u32 op, i32 fd, u64 address = 0) {
        astia_sqe sqe{};

        sqe.op      = op;
        sqe.fd      = fd;
        sqe.address = address;

        return sqe;
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
        mm::shptr<fs::open_file>          listener;
        mm::shptr<fs::open_file>          client;
        const fs::unix_socket_operations* listener_ops = nullptr;
        const fs::unix_socket_operations* client_ops   = nullptr;
        i32                               listener_fd  = -1;
    };

    bool prepare_accept(accept_pair& pair, const char* client_path) {
        using namespace astia::io::aio::test_support;

        if (fs::create_unix_socket(pair.listener, SOCK_STREAM) != 0 ||
            fs::create_unix_socket(pair.client, SOCK_STREAM) != 0)
            return false;

        if (!pair.listener || !pair.client)
            return false;

        pair.listener_ops = pair.listener->parent()->unix_socket_capability();
        pair.client_ops   = pair.client->parent()->unix_socket_capability();

        if (pair.listener_ops == nullptr || pair.client_ops == nullptr)
            return false;

        set_unix_lookup_endpoint(pair.listener->parent());

        if (client_path != nullptr &&
            pair.client_ops->bind(*pair.client, path_address(client_path), client_path) !=
                0)
            return false;

        if (pair.listener_ops->listen(*pair.listener, 1) != 0)
            return false;

        pair.listener_fd = install_descriptor(pair.listener);

        if (pair.listener_fd < 0)
            return false;

        return pair.client_ops->connect(*pair.client, socket_address(), "/") == 0;
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

    const auto* listener_ops = listener->parent()->unix_socket_capability();
    const auto* client_ops   = client->parent()->unix_socket_capability();

    ASTIA_CHECK(listener_ops != nullptr);
    ASTIA_CHECK(client_ops != nullptr);

    if (listener_ops == nullptr || client_ops == nullptr)
        return;

    ASTIA_CHECK(listener_ops->listen(*listener, 1) == 0);

    auto filler = aio_test::make_open_file(new aio_test::fake_file);

    while (install_descriptor(filler) >= 0) {
    }

    const astia_sockaddr address = socket_address();

    ASTIA_CHECK(client_ops->connect(*client, address, "/") == 0);
    ASTIA_CHECK(listener->poll(POLLIN) == POLLIN);

    io::request rejected;

    ASTIA_CHECK(listener_ops->accept(*listener, rejected) == -EMFILE);
    ASTIA_CHECK(listener->poll(POLLIN) == POLLIN);

    clear_descriptor(3);

    io::request accepted;

    ASTIA_CHECK(listener_ops->accept(*listener, accepted) == 3);
    ASTIA_CHECK(accepted.complete());
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

    io::request empty;

    ASTIA_CHECK(listener_ops->accept(*listener, empty) == 0);
    ASTIA_CHECK(empty.stage() == 1);
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

    const auto* ops = socket->parent()->unix_socket_capability();

    ASTIA_CHECK(ops != nullptr);

    if (ops == nullptr)
        return;

    ASTIA_CHECK(ops->bind(*socket, socket_address("/tmp/first"), "/tmp/first") == 0);
    ASTIA_CHECK(fs::unix_registration_count() == 1);
    ASTIA_CHECK(astia::string_equal(fs::unix_registration_path(0), "/tmp/first"));

    ASTIA_CHECK(ops->bind(*socket, socket_address("/tmp/second"), "/tmp/second") ==
                -EINVAL);
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

    const auto* ops = socket->parent()->unix_socket_capability();

    ASTIA_CHECK(ops != nullptr);

    if (ops == nullptr)
        return;

    ASTIA_CHECK(ops->bind(*socket, socket_address("/tmp/keep"), "/tmp/keep") == 0);
    ASTIA_CHECK(ops->bind(*socket, socket_address("/tmp/discard"), "/tmp/discard") ==
                -EINVAL);
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

    const auto* listener_ops = listener->parent()->unix_socket_capability();
    const auto* client_ops   = client->parent()->unix_socket_capability();

    ASTIA_CHECK(listener_ops != nullptr);
    ASTIA_CHECK(client_ops != nullptr);

    if (listener_ops == nullptr || client_ops == nullptr)
        return;

    ASTIA_CHECK(listener_ops->listen(*listener, 1) == 0);

    const astia_sockaddr address = socket_address("/s");

    ASTIA_CHECK(client_ops->connect(*client, address, "/s") == 0);
    ASTIA_CHECK(client_ops->connect(*client, address, "/s") == -EISCONN);
    ASTIA_CHECK(listener->poll(POLLIN) == POLLIN);

    io::request accepted;

    const long fd = listener_ops->accept(*listener, accepted);

    ASTIA_CHECK(fd >= 0);
    ASTIA_CHECK(accepted.complete());

    if (fd >= 0)
        clear_descriptor(static_cast<i32>(fd));

    io::request empty;

    ASTIA_CHECK(listener_ops->accept(*listener, empty) == 0);
    ASTIA_CHECK(empty.stage() == 1);
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

    const auto* listener_ops = listener->parent()->unix_socket_capability();
    const auto* client_ops   = client->parent()->unix_socket_capability();

    ASTIA_CHECK(listener_ops != nullptr);
    ASTIA_CHECK(client_ops != nullptr);

    if (listener_ops == nullptr || client_ops == nullptr)
        return;

    ASTIA_CHECK(listener_ops->listen(*listener, 1) == 0);

    const astia_sockaddr address = socket_address("/s");

    fs::lookup_blocked.store(true);

    std::atomic<long> first_result{-1};

    std::thread first(
        [&] { first_result.store(client_ops->connect(*client, address, "/s")); });

    while (!fs::lookup_entered.load()) {
        std::this_thread::yield();
    }

    ASTIA_CHECK(client_ops->connect(*client, address, "/s") == -EALREADY);

    fs::lookup_blocked.store(false);
    first.join();

    ASTIA_CHECK(first_result.load() == 0);
    ASTIA_CHECK(listener->poll(POLLIN) == POLLIN);

    io::request accepted;

    ASTIA_CHECK(listener_ops->accept(*listener, accepted) >= 0);
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

ASTIA_TEST(aio_unix_bind_registers_inside_caller_root) {
    using namespace astia::io::aio::test_support;

    reset_environment();
    install_current_process();
    fs::reset_unix_registrations();
    set_jail_directory();
    set_process_root("/jail");
    set_process_cwd("/jail");

    mm::shptr<fs::open_file> socket;

    ASTIA_CHECK(fs::create_unix_socket(socket, SOCK_STREAM) == 0);

    if (!socket)
        return;

    const i32 fd = install_descriptor(socket);

    ASTIA_CHECK(fd >= 3);

    astia_sockaddr       address = socket_address("/sock");
    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sqe = socket_sqe(OP_BIND, fd, reinterpret_cast<u64>(&address));

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.result(0) == 0);
    ASTIA_CHECK(fs::unix_registration_count() == 1);
    ASTIA_CHECK(string_equal(fs::unix_registration_path(0), "/jail/sock"));
    release_parked(slot);
}

ASTIA_TEST(aio_unix_connect_uses_caller_root) {
    using namespace astia::io::aio::test_support;

    reset_environment();
    install_current_process();
    fs::reset_unix_registrations();
    set_jail_directory();

    mm::shptr<fs::open_file> listener;

    ASTIA_CHECK(fs::create_unix_socket(listener, SOCK_STREAM) == 0);

    if (!listener)
        return;

    const auto* listener_ops = listener->parent()->unix_socket_capability();

    ASTIA_CHECK(listener_ops != nullptr && listener_ops->listen(*listener, 1) == 0);

    set_unix_lookup_endpoint(listener->parent());
    fs::require_unix_lookup_path("/jail/sock");

    astia_sockaddr address = socket_address("/sock");

    mm::shptr<fs::open_file> outside_client;

    ASTIA_CHECK(fs::create_unix_socket(outside_client, SOCK_STREAM) == 0);

    if (!outside_client)
        return;

    const i32 outside_fd = install_descriptor(outside_client);

    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            outside_sqe =
        socket_sqe(OP_CONNECT, outside_fd, reinterpret_cast<u64>(&address));

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, outside_sqe));
    ASTIA_CHECK(capture.result(0) == -ECONNREFUSED);
    ASTIA_CHECK(string_equal(fs::unix_lookup_path(), "/sock"));

    set_process_root("/jail");
    set_process_cwd("/jail");

    mm::shptr<fs::open_file> inside_client;

    ASTIA_CHECK(fs::create_unix_socket(inside_client, SOCK_STREAM) == 0);

    if (!inside_client)
        return;

    const i32 inside_fd = install_descriptor(inside_client);

    astia_sqe inside_sqe =
        socket_sqe(OP_CONNECT, inside_fd, reinterpret_cast<u64>(&address));

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, inside_sqe));
    ASTIA_CHECK(capture.result(1) == 0);
    ASTIA_CHECK(string_equal(fs::unix_lookup_path(), "/jail/sock"));
    ASTIA_CHECK(listener->poll(POLLIN) == POLLIN);
    release_parked(slot);
}

ASTIA_TEST(aio_unix_bind_relative_path_uses_caller_root) {
    using namespace astia::io::aio::test_support;

    reset_environment();
    install_current_process();
    fs::reset_unix_registrations();
    set_jail_directory();
    set_process_root("/jail");
    set_process_cwd("/jail");

    mm::shptr<fs::open_file> socket;

    ASTIA_CHECK(fs::create_unix_socket(socket, SOCK_STREAM) == 0);

    if (!socket)
        return;

    const i32 fd = install_descriptor(socket);

    astia_sockaddr       address = socket_address("sock");
    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sqe = socket_sqe(OP_BIND, fd, reinterpret_cast<u64>(&address));

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.result(0) == 0);
    ASTIA_CHECK(fs::unix_registration_count() == 1);
    ASTIA_CHECK(string_equal(fs::unix_registration_path(0), "/jail/sock"));
    release_parked(slot);
}

ASTIA_TEST(aio_unix_bind_rejects_host_path_from_inside_chroot) {
    using namespace astia::io::aio::test_support;

    reset_environment();
    install_current_process();
    fs::reset_unix_registrations();
    set_jail_directory();

    mm::shptr<fs::open_file> host_socket;

    ASTIA_CHECK(fs::create_unix_socket(host_socket, SOCK_STREAM) == 0);

    if (!host_socket)
        return;

    const i32 host_fd = install_descriptor(host_socket);

    astia_sockaddr       address = socket_address("/jail/sock");
    cq_capture           capture;
    aio::ring::ring_slot slot{};

    ASTIA_CHECK(aio::registry::dispatch(
        slot, &capture.ring,
        socket_sqe(OP_BIND, host_fd, reinterpret_cast<u64>(&address))));
    ASTIA_CHECK(capture.result(0) == 0);
    ASTIA_CHECK(fs::unix_registration_count() == 1);
    ASTIA_CHECK(string_equal(fs::unix_registration_path(0), "/jail/sock"));

    set_process_root("/jail");
    set_process_cwd("/jail");

    mm::shptr<fs::open_file> jailed_socket;

    ASTIA_CHECK(fs::create_unix_socket(jailed_socket, SOCK_STREAM) == 0);

    if (!jailed_socket)
        return;

    const i32 jailed_fd = install_descriptor(jailed_socket);

    ASTIA_CHECK(aio::registry::dispatch(
        slot, &capture.ring,
        socket_sqe(OP_BIND, jailed_fd, reinterpret_cast<u64>(&address))));
    ASTIA_CHECK(capture.result(1) == -EACCES);
    ASTIA_CHECK(fs::unix_registration_count() == 1);
    release_parked(slot);
}

ASTIA_TEST(aio_unix_sendto_uses_caller_root) {
    using namespace astia::io::aio::test_support;

    reset_environment();
    install_current_process();
    fs::reset_unix_registrations();
    set_jail_directory();

    mm::shptr<fs::open_file> target;

    ASTIA_CHECK(fs::create_unix_socket(target, SOCK_DGRAM) == 0);

    if (!target)
        return;

    set_unix_lookup_endpoint(target->parent());
    fs::require_unix_lookup_path("/jail/sock");

    char byte = 'x';

    astia_sockaddr       address = socket_address("/sock");
    cq_capture           capture;
    aio::ring::ring_slot slot{};

    mm::shptr<fs::open_file> outside_client;

    ASTIA_CHECK(fs::create_unix_socket(outside_client, SOCK_DGRAM) == 0);

    if (!outside_client)
        return;

    const i32 outside_fd = install_descriptor(outside_client);

    astia_sqe outside_sqe =
        socket_sqe(OP_SENDTO, outside_fd, reinterpret_cast<u64>(&byte));

    outside_sqe.offset = reinterpret_cast<u64>(&address);
    outside_sqe.length = 1;

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, outside_sqe));
    ASTIA_CHECK(capture.result(0) == -ECONNREFUSED);
    ASTIA_CHECK(string_equal(fs::unix_lookup_path(), "/sock"));

    set_process_root("/jail");
    set_process_cwd("/jail");

    mm::shptr<fs::open_file> inside_client;

    ASTIA_CHECK(fs::create_unix_socket(inside_client, SOCK_DGRAM) == 0);

    if (!inside_client)
        return;

    const i32 inside_fd = install_descriptor(inside_client);

    astia_sqe inside_sqe = socket_sqe(OP_SENDTO, inside_fd, reinterpret_cast<u64>(&byte));

    inside_sqe.offset = reinterpret_cast<u64>(&address);
    inside_sqe.length = 1;

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, inside_sqe));
    ASTIA_CHECK(capture.result(1) == 1);
    ASTIA_CHECK(string_equal(fs::unix_lookup_path(), "/jail/sock"));
    release_parked(slot);
}
