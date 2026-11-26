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

    i32 submit_openat(char* path, u32 flags) {
        using namespace aio::test_support;

        cq_capture           capture;
        aio::ring::ring_slot slot{};
        astia_sqe            sqe = sqe_for(OP_OPENAT);

        sqe.address  = reinterpret_cast<u64>(path);
        sqe.op_flags = flags;

        if (!aio::registry::dispatch(slot, &capture.ring, sqe))
            return -EIO;

        return capture.result(0);
    }

    i32 submit_linkat(char* old_path, char* new_path) {
        using namespace aio::test_support;

        cq_capture           capture;
        aio::ring::ring_slot slot{};
        astia_sqe            sqe = sqe_for(OP_LINKAT);

        sqe.address = reinterpret_cast<u64>(old_path);
        sqe.offset  = reinterpret_cast<u64>(new_path);

        if (!aio::registry::dispatch(slot, &capture.ring, sqe))
            return -EIO;

        return capture.result(0);
    }

    i32 submit_chown_path(char* path, u32 owner, u64 group, u32 flags = 0) {
        using namespace aio::test_support;

        cq_capture           capture;
        aio::ring::ring_slot slot{};
        astia_sqe            sqe = sqe_for(OP_CHOWN);

        sqe.address  = reinterpret_cast<u64>(path);
        sqe.aux      = owner;
        sqe.offset   = group;
        sqe.op_flags = flags;

        if (!aio::registry::dispatch(slot, &capture.ring, sqe))
            return -EIO;

        return capture.result(0);
    }

    i32 submit_fchown(i32 fd, u32 owner, u64 group) {
        using namespace aio::test_support;

        cq_capture           capture;
        aio::ring::ring_slot slot{};
        astia_sqe            sqe = sqe_for(OP_CHOWN, fd);

        sqe.aux    = owner;
        sqe.offset = group;

        if (!aio::registry::dispatch(slot, &capture.ring, sqe))
            return -EIO;

        return capture.result(0);
    }

    void set_directory(const char* path, u16 permissions, uid_t owner, gid_t group) {
        using namespace aio::test_support;

        set_node_mode(path, fs::file_mode(fs::file_type::directory,
                                          fs::file_permissions(permissions)));
        set_node_owner(path, owner, group);
    }

    void set_file(const char* path, u16 permissions, uid_t owner, gid_t group) {
        using namespace aio::test_support;

        set_node_mode(path, fs::file_mode(fs::file_type::regular,
                                          fs::file_permissions(permissions)));
        set_node_owner(path, owner, group);
    }

    void restore_credentials() {
        aio::test_support::set_process_credentials(uid_t{0}, gid_t{0});
    }
} // namespace

ASTIA_TEST(aio_filesystem_openat_installs_production_descriptor) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_node_mode(astia::fs::file_mode(astia::fs::file_type::regular));

    auto* node = new aio_test::fake_file;

    set_open_file(aio_test::make_open_file(node));

    char path[] = "/tmp/aio";

    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sqe = sqe_for(OP_OPENAT);

    sqe.address  = reinterpret_cast<u64>(path);
    sqe.op_flags = 0;

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.result(0) == 3);
    ASTIA_CHECK(descriptor_present(3));
}

ASTIA_TEST(aio_filesystem_openat_reuses_lowest_free_descriptor) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    clear_descriptor(0);
    set_node_mode(astia::fs::file_mode(astia::fs::file_type::regular));
    set_open_file(aio_test::make_open_file(new aio_test::fake_file));

    char path[] = "/tmp/aio";

    ASTIA_CHECK(submit_openat(path, 0) == 0);
    ASTIA_CHECK(descriptor_present(0));
}

ASTIA_TEST(aio_filesystem_openat_create_requires_writable_parent) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_node_mode(fs::file_mode{});
    set_directory("/root", 0755, uid_t{0}, gid_t{0});
    set_open_file(aio_test::make_open_file(new aio_test::fake_file));

    char path[] = "/root/planted";

    ASTIA_CHECK(submit_openat(path, ASTIA_AIO_O_CREAT) == -EACCES);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_openat_create_allows_writable_parent) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_node_mode(fs::file_mode{});
    set_directory("/shared", 0777, uid_t{0}, gid_t{0});
    set_open_file(aio_test::make_open_file(new aio_test::fake_file));

    char path[] = "/shared/new";

    ASTIA_CHECK(submit_openat(path, ASTIA_AIO_O_CREAT) == 3);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_openat_create_allows_root_in_writable_parent) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_node_mode(fs::file_mode{});
    set_directory("/root", 0755, uid_t{0}, gid_t{0});
    set_open_file(aio_test::make_open_file(new aio_test::fake_file));

    char path[] = "/root/new";

    ASTIA_CHECK(submit_openat(path, ASTIA_AIO_O_CREAT) == 3);
}

ASTIA_TEST(aio_filesystem_openat_create_requires_parent_search) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_node_mode(fs::file_mode{});
    set_directory("/private", 0600, uid_t{0}, gid_t{0});
    set_open_file(aio_test::make_open_file(new aio_test::fake_file));

    char path[] = "/private/new";

    ASTIA_CHECK(submit_openat(path, ASTIA_AIO_O_CREAT) == -EACCES);
}

ASTIA_TEST(aio_filesystem_openat_create_keeps_existing_file_access) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_directory("/root", 0755, uid_t{0}, gid_t{0});
    set_node_mode("/root/existing",
                  fs::file_mode(fs::file_type::regular, fs::file_permissions(0666)));
    set_open_file(aio_test::make_open_file(new aio_test::fake_file));

    char path[] = "/root/existing";

    ASTIA_CHECK(submit_openat(path, ASTIA_AIO_O_CREAT | ASTIA_AIO_O_WRONLY) == 3);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_openat_write_only_requires_write_permission) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_file("/tmp/readonly", 0644, uid_t{0}, gid_t{0});
    set_open_file(aio_test::make_open_file(new aio_test::fake_file));

    char path[] = "/tmp/readonly";

    ASTIA_CHECK(submit_openat(path, ASTIA_AIO_O_WRONLY) == -EACCES);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_openat_read_write_requires_write_permission) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_file("/tmp/readonly", 0644, uid_t{0}, gid_t{0});
    set_open_file(aio_test::make_open_file(new aio_test::fake_file));

    char path[] = "/tmp/readonly";

    ASTIA_CHECK(submit_openat(path, ASTIA_AIO_O_RDWR) == -EACCES);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_openat_read_write_requires_read_permission) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_file("/tmp/writeonly", 0200, uid_t{1000}, gid_t{1000});
    set_open_file(aio_test::make_open_file(new aio_test::fake_file));

    char path[] = "/tmp/writeonly";

    ASTIA_CHECK(submit_openat(path, ASTIA_AIO_O_RDWR) == -EACCES);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_openat_read_only_requires_read_permission) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_file("/tmp/writeonly", 0200, uid_t{1000}, gid_t{1000});
    set_open_file(aio_test::make_open_file(new aio_test::fake_file));

    char path[] = "/tmp/writeonly";

    ASTIA_CHECK(submit_openat(path, ASTIA_AIO_O_RDONLY) == -EACCES);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_openat_read_only_allows_read_permission) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_file("/tmp/readable", 0004, uid_t{0}, gid_t{0});
    set_open_file(aio_test::make_open_file(new aio_test::fake_file));

    char path[] = "/tmp/readable";

    ASTIA_CHECK(submit_openat(path, ASTIA_AIO_O_RDONLY) == 3);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_openat_write_only_allows_owner_write_permission) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_file("/tmp/owned", 0200, uid_t{1000}, gid_t{1000});
    set_open_file(aio_test::make_open_file(new aio_test::fake_file));

    char path[] = "/tmp/owned";

    ASTIA_CHECK(submit_openat(path, ASTIA_AIO_O_WRONLY) == 3);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_openat_write_only_allows_group_write_permission) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_file("/tmp/shared", 0020, uid_t{0}, gid_t{1000});
    set_open_file(aio_test::make_open_file(new aio_test::fake_file));

    char path[] = "/tmp/shared";

    ASTIA_CHECK(submit_openat(path, ASTIA_AIO_O_WRONLY) == 3);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_openat_write_only_allows_root) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_file("/tmp/protected", 0444, uid_t{0}, gid_t{0});
    set_open_file(aio_test::make_open_file(new aio_test::fake_file));

    char path[] = "/tmp/protected";

    ASTIA_CHECK(submit_openat(path, ASTIA_AIO_O_WRONLY) == 3);
}

ASTIA_TEST(aio_filesystem_linkat_allows_read_only_source_parent) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_node_mode(fs::file_mode{});
    set_directory("/src", 0555, uid_t{0}, gid_t{0});
    set_node_mode("/src/file",
                  fs::file_mode(fs::file_type::regular, fs::file_permissions(0644)));
    set_directory("/dst", 0755, uid_t{1000}, gid_t{1000});

    char old_path[] = "/src/file";
    char new_path[] = "/dst/link";

    ASTIA_CHECK(submit_linkat(old_path, new_path) == 0);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_linkat_requires_source_parent_search) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_node_mode(fs::file_mode{});
    set_directory("/src", 0644, uid_t{0}, gid_t{0});
    set_node_mode("/src/file",
                  fs::file_mode(fs::file_type::regular, fs::file_permissions(0644)));
    set_directory("/dst", 0755, uid_t{1000}, gid_t{1000});

    char old_path[] = "/src/file";
    char new_path[] = "/dst/link";

    ASTIA_CHECK(submit_linkat(old_path, new_path) == -EACCES);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_linkat_requires_writable_destination_parent) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_node_mode(fs::file_mode{});
    set_directory("/src", 0755, uid_t{0}, gid_t{0});
    set_node_mode("/src/file",
                  fs::file_mode(fs::file_type::regular, fs::file_permissions(0644)));
    set_directory("/dst", 0555, uid_t{1000}, gid_t{1000});

    char old_path[] = "/src/file";
    char new_path[] = "/dst/link";

    ASTIA_CHECK(submit_linkat(old_path, new_path) == -EACCES);

    restore_credentials();
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

    auto* node = new aio_test::fake_file;

    node->sync_result = 0;

    const auto file = aio_test::make_open_file(node, ASTIA_AIO_O_RDWR);

    const i32 fd = install_descriptor(file);

    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sync = sqe_for(OP_FSYNC, fd);

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sync));
    ASTIA_CHECK(capture.result(0) == 0);

    astia_sqe truncate = sqe_for(OP_FTRUNCATE, fd);

    truncate.offset = 9;

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, truncate));
    ASTIA_CHECK(capture.result(1) == 0);
    ASTIA_CHECK(node->size_value == 9);
}

ASTIA_TEST(aio_filesystem_statx_reports_production_size) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();

    auto* node = new aio_test::fake_file;

    node->size_value = 37;

    const auto file = aio_test::make_open_file(node, ASTIA_AIO_O_RDWR);

    const i32 fd = install_descriptor(file);

    astia_stat           stat{};
    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sqe = sqe_for(OP_STATX, fd);

    sqe.offset = reinterpret_cast<u64>(&stat);
    sqe.length = sizeof(stat);

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.result(0) == 0);
    ASTIA_CHECK(stat.size == 37);
}

ASTIA_TEST(aio_filesystem_chown_owner_sentinel_keeps_existing_owner) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1234});
    set_node_owner("/owned", uid_t{1000}, gid_t{4242});

    char path[] = "/owned";

    ASTIA_CHECK(submit_chown_path(path, 0xffffffffu, 1234) == 0);

    const chown_record& call = last_chown_path();

    ASTIA_CHECK(call.called);
    ASTIA_CHECK(call.uid == uid_t{1000});
    ASTIA_CHECK(call.gid == gid_t{1234});
}

ASTIA_TEST(aio_filesystem_chown_group_sentinel_keeps_existing_group) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_node_owner("/owned", uid_t{1000}, gid_t{4242});

    char path[] = "/owned";

    ASTIA_CHECK(submit_chown_path(path, 1000, 0xffffffffull) == 0);

    const chown_record& call = last_chown_path();

    ASTIA_CHECK(call.called);
    ASTIA_CHECK(call.uid == uid_t{1000});
    ASTIA_CHECK(call.gid == gid_t{4242});
}

ASTIA_TEST(aio_filesystem_chown_rejects_ids_above_32_bits) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_node_owner("/owned", uid_t{1000}, gid_t{1000});

    char path[] = "/owned";

    ASTIA_CHECK(submit_chown_path(path, 1000, 0x100000000ull) == -EINVAL);
    ASTIA_CHECK(!last_chown_path().called);
}

ASTIA_TEST(aio_filesystem_fchown_owner_sentinel_keeps_existing_owner) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1234});

    auto* node = new aio_test::fake_file;

    node->uid_value = uid_t{1000};
    node->gid_value = gid_t{4242};

    const i32 fd = install_descriptor(aio_test::make_open_file(node, ASTIA_AIO_O_RDWR));

    ASTIA_CHECK(fd >= 3);
    ASTIA_CHECK(submit_fchown(fd, 0xffffffffu, 1234) == 0);
    ASTIA_CHECK(node->chown_called);
    ASTIA_CHECK(node->chown_uid == uid_t{1000});
    ASTIA_CHECK(node->chown_gid == gid_t{1234});
}

ASTIA_TEST(aio_filesystem_fchown_group_sentinel_keeps_existing_group) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});

    auto* node = new aio_test::fake_file;

    node->uid_value = uid_t{1000};
    node->gid_value = gid_t{4242};

    const i32 fd = install_descriptor(aio_test::make_open_file(node, ASTIA_AIO_O_RDWR));

    ASTIA_CHECK(fd >= 3);
    ASTIA_CHECK(submit_fchown(fd, 1000, 0xffffffffull) == 0);
    ASTIA_CHECK(node->chown_called);
    ASTIA_CHECK(node->chown_uid == uid_t{1000});
    ASTIA_CHECK(node->chown_gid == gid_t{4242});
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

    char path[] = "/tmp/aio";

    astia_sqe openat = sqe_for(OP_OPENAT);

    openat.address = reinterpret_cast<u64>(path);

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, openat));
    ASTIA_CHECK(capture.result(1) == -EFAULT);
}
