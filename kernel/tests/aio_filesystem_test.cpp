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

ASTIA_TEST(aio_filesystem_openat_installs_production_descriptor) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_node_mode(astia::fs::file_mode(astia::fs::file_type::regular));

    auto* node = new aio_test::fake_file;
    set_open_file(aio_test::make_open_file(node));

    char                 path[] = "/tmp/aio";
    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sqe = sqe_for(OP_OPENAT);
    sqe.address              = reinterpret_cast<u64>(path);
    sqe.op_flags             = 0;

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.result(0) == 3);
    ASTIA_CHECK(descriptor_present(3));
}

ASTIA_TEST(aio_filesystem_close_releases_descriptor) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    const i32 fd = install_descriptor(aio_test::make_open_file(new aio_test::fake_file));

    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sqe = sqe_for(OP_CLOSE, fd);

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.result(0) == 0);
    ASTIA_CHECK(!descriptor_present(fd));
}

ASTIA_TEST(aio_filesystem_fsync_and_ftruncate_use_production_file) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    auto* node        = new aio_test::fake_file;
    node->sync_result = 0;
    const auto file   = aio_test::make_open_file(node, ASTIA_AIO_O_RDWR);
    const i32  fd     = install_descriptor(file);

    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sync = sqe_for(OP_FSYNC, fd);
    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sync));
    ASTIA_CHECK(capture.result(0) == 0);

    astia_sqe truncate = sqe_for(OP_FTRUNCATE, fd);
    truncate.offset    = 9;
    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, truncate));
    ASTIA_CHECK(capture.result(1) == 0);
    ASTIA_CHECK(node->size_value == 9);
}

ASTIA_TEST(aio_filesystem_statx_reports_production_size) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    auto* node       = new aio_test::fake_file;
    node->size_value = 37;
    const auto file  = aio_test::make_open_file(node, ASTIA_AIO_O_RDWR);
    const i32  fd    = install_descriptor(file);

    astia_stat           stat{};
    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sqe = sqe_for(OP_STATX, fd);
    sqe.offset               = reinterpret_cast<u64>(&stat);
    sqe.length               = sizeof(stat);

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.result(0) == 0);
    ASTIA_CHECK(stat.size == 37);
}

ASTIA_TEST(aio_filesystem_errors_stay_on_production_path) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();

    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            close = sqe_for(OP_CLOSE, 99);
    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, close));
    ASTIA_CHECK(capture.result(0) == -EBADF);

    set_memory_allowed(false);
    char      path[] = "/tmp/aio";
    astia_sqe openat = sqe_for(OP_OPENAT);
    openat.address   = reinterpret_cast<u64>(path);
    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, openat));
    ASTIA_CHECK(capture.result(1) == -EFAULT);
}
