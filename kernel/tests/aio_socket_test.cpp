#include "aio_registry_test_support.hpp"
#include "support/aio_test_file.hpp"
#include "support/test.hpp"

#ifdef RENAME_NOREPLACE
#undef RENAME_NOREPLACE
#endif
#ifdef RENAME_EXCHANGE
#undef RENAME_EXCHANGE
#endif
#ifdef RENAME_WHITEOUT
#undef RENAME_WHITEOUT
#endif

#include <astia/api/aio.h>

#include <io/aio/registry.hpp>
#include <io/aio/ring.hpp>

#include <cstdio>

using namespace astia;

namespace {

    namespace aio = astia::io::aio;

    astia_sqe sqe_for(u32 op, i32 fd = -1) {
        astia_sqe sqe{};
        sqe.op = op;
        sqe.fd = fd;
        return sqe;
    }

} // namespace

ASTIA_TEST(aio_socket_pipe_installs_production_endpoints) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_pipe_files(aio_test::make_open_file(new aio_test::fake_file),
                   aio_test::make_open_file(new aio_test::fake_file));

    i32                  fds[2] = {-1, -1};
    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sqe = sqe_for(OP_PIPE);
    sqe.address              = reinterpret_cast<u64>(fds);

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.result(0) == 0);
    ASTIA_CHECK(fds[0] >= 3 && fds[1] >= 3);
    ASTIA_CHECK(descriptor_present(fds[0]));
    ASTIA_CHECK(descriptor_present(fds[1]));
}

ASTIA_TEST(aio_socket_creates_production_socket_descriptor) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_socket_file(aio_test::make_open_file(new aio_test::fake_file));

    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sqe = sqe_for(OP_SOCKET, AF_UNIX);
    sqe.op_flags             = SOCK_STREAM;
    sqe.aux                  = 0;

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.result(0) >= 3);
    ASTIA_CHECK(descriptor_present(capture.result(0)));
}

ASTIA_TEST(aio_socket_rejects_unsupported_domain_and_protocol) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();

    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sqe = sqe_for(OP_SOCKET, AF_UNIX);
    sqe.op_flags             = SOCK_STREAM;
    sqe.aux                  = 1;

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.result(0) == -EAFNOSUPPORT);

    astia_sqe pair = sqe_for(OP_SOCKETPAIR, AF_UNIX);
    pair.op_flags  = SOCK_STREAM;
    pair.length    = 2 * sizeof(i32);
    i32 fds[2]     = {-1, -1};
    pair.address   = reinterpret_cast<u64>(fds);

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, pair));
    ASTIA_CHECK(capture.result(1) == -EMFILE);
}

ASTIA_TEST(aio_socketpair_installs_production_descriptor_pair) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_socketpair_files(aio_test::make_open_file(new aio_test::fake_file),
                         aio_test::make_open_file(new aio_test::fake_file));

    i32                  fds[2] = {-1, -1};
    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sqe = sqe_for(OP_SOCKETPAIR, AF_UNIX);
    sqe.op_flags             = SOCK_STREAM;
    sqe.aux                  = 0;
    sqe.length               = 2 * sizeof(i32);
    sqe.address              = reinterpret_cast<u64>(fds);

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.result(0) == 0);
    ASTIA_CHECK(fds[0] >= 3 && fds[1] >= 3);
    ASTIA_CHECK(descriptor_present(fds[0]));
    ASTIA_CHECK(descriptor_present(fds[1]));
}

ASTIA_TEST(aio_socket_bind_listen_shutdown_use_production_file) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_node_mode(astia::fs::file_mode(astia::fs::file_type::directory,
                                       astia::fs::default_directory_permissions));
    const i32 fd = install_descriptor(aio_test::make_open_file(new aio_test::fake_file));

    cq_capture           capture;
    aio::ring::ring_slot slot{};

    astia_sockaddr address{};
    address.family  = AF_UNIX;
    address.len     = 3;
    address.data[0] = '/';

    astia_sqe bind = sqe_for(OP_BIND, fd);
    bind.address   = reinterpret_cast<u64>(&address);
    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, bind));
    ASTIA_CHECK(capture.result(0) == -EOPNOTSUPP);

    astia_sqe listen = sqe_for(OP_LISTEN, fd);
    listen.aux       = 1;
    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, listen));
    ASTIA_CHECK(capture.result(1) == -EOPNOTSUPP);

    astia_sqe shutdown = sqe_for(OP_SHUTDOWN, fd);
    shutdown.op_flags  = 1;
    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, shutdown));
    ASTIA_CHECK(capture.result(2) == -EOPNOTSUPP);
}

ASTIA_TEST(aio_socket_recvfrom_zero_length_completes_zero) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    const i32 fd = install_descriptor(aio_test::make_open_file(new aio_test::fake_file));

    char                 byte = 0;
    astia_sockaddr       address{};
    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sqe = sqe_for(OP_RECVFROM, fd);
    sqe.address              = reinterpret_cast<u64>(&byte);
    sqe.offset               = reinterpret_cast<u64>(&address);
    sqe.length               = 0;

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.result(0) == 0);
}
