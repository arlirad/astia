#include <astia/api/aio.h>
#include <astia/api/errno.h>

#include "aio_registry_test_support.hpp"
#include "support/aio_test_file.hpp"
#include "support/test.hpp"
#include <ipc/unix_socket.hpp>
#include <proc/proc.hpp>

using namespace astia;

namespace astia::fs {
    mm::shptr<file> unix_lookup_endpoint;

    long register_unix_path(const char*, const mm::shptr<file>&, uid_t, gid_t,
                            astia::span<char>) {
        return 0;
    }

    long lookup_unix_path(const char*, mm::shptr<file>& endpoint) {
        if (!unix_lookup_endpoint)
            return -ENOENT;

        endpoint = unix_lookup_endpoint;

        return 0;
    }

    void unregister_unix_path(const char*, file*) {}
} // namespace astia::fs

namespace {
    namespace fs = astia::fs;

    void set_unix_lookup_endpoint(const astia::mm::shptr<fs::file>& endpoint) {
        fs::unix_lookup_endpoint = endpoint;
    }

    astia_sockaddr socket_address() {
        astia_sockaddr address{};

        address.family  = AF_UNIX;
        address.len     = 3;
        address.data[0] = '/';

        return address;
    }
} // namespace

ASTIA_TEST(unix_socket_accept_retains_queued_connection_when_fds_exhausted) {
    using namespace astia::io::aio::test_support;

    reset_environment();
    install_current_process();

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

    ASTIA_CHECK(client_ops->connect(*client, address) == 0);
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
