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

    i32 submit_readdir(i32 fd, astia_dirent* out, u64 index) {
        using namespace aio::test_support;

        cq_capture           capture;
        aio::ring::ring_slot slot{};
        astia_sqe            sqe = sqe_for(OP_READDIR, fd);

        sqe.offset   = index;
        sqe.address  = reinterpret_cast<u64>(out);
        sqe.op_flags = ASTIA_AIO_READDIR_FD;

        if (!aio::registry::dispatch(slot, &capture.ring, sqe))
            return -EIO;

        return capture.result(0);
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

    i32 submit_statx(char* path, i32 dirfd, u32 op_flags, astia_stat* out) {
        using namespace aio::test_support;

        cq_capture           capture;
        aio::ring::ring_slot slot{};
        astia_sqe            sqe = sqe_for(OP_STATX, dirfd);

        sqe.address  = reinterpret_cast<u64>(path);
        sqe.offset   = reinterpret_cast<u64>(out);
        sqe.length   = sizeof(astia_stat);
        sqe.op_flags = op_flags;

        if (!aio::registry::dispatch(slot, &capture.ring, sqe))
            return -EIO;

        return capture.result(0);
    }

    i32 submit_unlinkat(char* path, u32 flags = 0) {
        using namespace aio::test_support;

        cq_capture           capture;
        aio::ring::ring_slot slot{};
        astia_sqe            sqe = sqe_for(OP_UNLINKAT);

        sqe.address  = reinterpret_cast<u64>(path);
        sqe.op_flags = flags;

        if (!aio::registry::dispatch(slot, &capture.ring, sqe))
            return -EIO;

        return capture.result(0);
    }

    i32 submit_mkdirat(char* path, u32 mode) {
        using namespace aio::test_support;

        cq_capture           capture;
        aio::ring::ring_slot slot{};
        astia_sqe            sqe = sqe_for(OP_MKDIRAT);

        sqe.address = reinterpret_cast<u64>(path);
        sqe.aux     = mode;

        if (!aio::registry::dispatch(slot, &capture.ring, sqe))
            return -EIO;

        return capture.result(0);
    }

    i32 submit_symlinkat(char* target, char* path) {
        using namespace aio::test_support;

        cq_capture           capture;
        aio::ring::ring_slot slot{};
        astia_sqe            sqe = sqe_for(OP_SYMLINKAT);

        sqe.address = reinterpret_cast<u64>(target);
        sqe.offset  = reinterpret_cast<u64>(path);

        if (!aio::registry::dispatch(slot, &capture.ring, sqe))
            return -EIO;

        return capture.result(0);
    }

    i32 submit_utimensat(char* path) {
        using namespace aio::test_support;

        cq_capture           capture;
        aio::ring::ring_slot slot{};
        astia_sqe            sqe = sqe_for(OP_UTIMENSAT);

        sqe.address = reinterpret_cast<u64>(path);

        if (!aio::registry::dispatch(slot, &capture.ring, sqe))
            return -EIO;

        return capture.result(0);
    }

    i32 submit_readdir_path(char* path, u64 index, astia_dirent* out) {
        using namespace aio::test_support;

        cq_capture           capture;
        aio::ring::ring_slot slot{};
        astia_sqe            sqe = sqe_for(OP_READDIR, static_cast<i32>(index));

        sqe.address = reinterpret_cast<u64>(out);
        sqe.offset  = reinterpret_cast<u64>(path);
        sqe.length  = sizeof *out;

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

ASTIA_TEST(aio_filesystem_openat_preserves_close_on_exec) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    clear_descriptor(0);
    set_node_mode(astia::fs::file_mode(astia::fs::file_type::regular));
    set_open_file(aio_test::make_open_file(new aio_test::fake_file));

    char path[] = "/tmp/aio";

    ASTIA_CHECK(submit_openat(path, ASTIA_AIO_O_CLOEXEC) == 0);
    ASTIA_CHECK(descriptor_flags(0) == ASTIA_FD_CLOEXEC);
}

ASTIA_TEST(aio_filesystem_openat_reports_emfile_when_table_full) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_node_mode(astia::fs::file_mode(astia::fs::file_type::regular));
    set_open_file(aio_test::make_open_file(new aio_test::fake_file));

    for (i32 fd = 3; fd < static_cast<i32>(proc::max_process_fds); ++fd) {
        ASTIA_CHECK(
            install_descriptor(aio_test::make_open_file(new aio_test::fake_file)) == fd);
    }

    char path[] = "/tmp/aio";

    ASTIA_CHECK(submit_openat(path, 0) == -EMFILE);
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

ASTIA_TEST(aio_filesystem_openat_requires_search_on_parent) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_directory("/secret", 0700, uid_t{0}, gid_t{0});
    set_file("/secret/file", 0644, uid_t{0}, gid_t{0});
    set_open_file(aio_test::make_open_file(new aio_test::fake_file));

    char path[] = "/secret/file";

    ASTIA_CHECK(submit_openat(path, 0) == -EACCES);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_openat_requires_search_on_every_ancestor) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_directory("/locked", 0700, uid_t{0}, gid_t{0});
    set_directory("/locked/open", 0755, uid_t{0}, gid_t{0});
    set_file("/locked/open/file", 0644, uid_t{0}, gid_t{0});
    set_open_file(aio_test::make_open_file(new aio_test::fake_file));

    char path[] = "/locked/open/file";

    ASTIA_CHECK(submit_openat(path, 0) == -EACCES);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_openat_allows_searchable_parent) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_directory("/shared", 0755, uid_t{0}, gid_t{0});
    set_file("/shared/file", 0644, uid_t{0}, gid_t{0});
    set_open_file(aio_test::make_open_file(new aio_test::fake_file));

    char path[] = "/shared/file";

    ASTIA_CHECK(submit_openat(path, 0) == 3);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_openat_root_bypasses_search) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_directory("/secret", 0600, uid_t{0}, gid_t{0});
    set_file("/secret/file", 0644, uid_t{0}, gid_t{0});
    set_open_file(aio_test::make_open_file(new aio_test::fake_file));

    char path[] = "/secret/file";

    ASTIA_CHECK(submit_openat(path, 0) == 3);
}

ASTIA_TEST(aio_filesystem_statx_requires_search_on_parent) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_directory("/secret", 0700, uid_t{0}, gid_t{0});
    set_file("/secret/file", 0644, uid_t{0}, gid_t{0});

    astia_stat stat{};

    char path[] = "/secret/file";

    ASTIA_CHECK(submit_statx(path, -1, 0, &stat) == -EACCES);

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

ASTIA_TEST(aio_filesystem_unlinkat_requires_writable_parent) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_directory("/root", 0755, uid_t{0}, gid_t{0});
    set_file("/root/victim", 0644, uid_t{0}, gid_t{0});

    char path[] = "/root/victim";

    ASTIA_CHECK(submit_unlinkat(path) == -EACCES);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_unlinkat_removedir_requires_writable_parent) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_directory("/root", 0755, uid_t{0}, gid_t{0});
    set_directory("/root/child", 0755, uid_t{0}, gid_t{0});

    char path[] = "/root/child";

    ASTIA_CHECK(submit_unlinkat(path, UNLINKAT_REMOVEDIR) == -EACCES);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_unlinkat_allows_writable_parent) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_directory("/shared", 0777, uid_t{0}, gid_t{0});
    set_file("/shared/victim", 0644, uid_t{0}, gid_t{0});

    char path[] = "/shared/victim";

    ASTIA_CHECK(submit_unlinkat(path) == 0);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_unlinkat_root_bypasses_writable_parent) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_directory("/root", 0555, uid_t{0}, gid_t{0});
    set_file("/root/victim", 0644, uid_t{0}, gid_t{0});

    char path[] = "/root/victim";

    ASTIA_CHECK(submit_unlinkat(path) == 0);
}

ASTIA_TEST(aio_filesystem_mkdirat_requires_writable_parent) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_directory("/root", 0755, uid_t{0}, gid_t{0});

    char path[] = "/root/planted";

    ASTIA_CHECK(submit_mkdirat(path, 0755) == -EACCES);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_mkdirat_allows_writable_parent) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_directory("/shared", 0777, uid_t{0}, gid_t{0});

    char path[] = "/shared/new";

    ASTIA_CHECK(submit_mkdirat(path, 0755) == 0);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_mkdirat_root_bypasses_writable_parent) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_directory("/root", 0555, uid_t{0}, gid_t{0});

    char path[] = "/root/new";

    ASTIA_CHECK(submit_mkdirat(path, 0755) == 0);
}

ASTIA_TEST(aio_filesystem_symlinkat_requires_writable_parent) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_directory("/root", 0755, uid_t{0}, gid_t{0});

    char target[] = "/etc/passwd";
    char path[]   = "/root/planted";

    ASTIA_CHECK(submit_symlinkat(target, path) == -EACCES);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_symlinkat_allows_writable_parent) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_directory("/shared", 0777, uid_t{0}, gid_t{0});

    char target[] = "/etc/passwd";
    char path[]   = "/shared/link";

    ASTIA_CHECK(submit_symlinkat(target, path) == 0);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_symlinkat_root_bypasses_writable_parent) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_directory("/root", 0555, uid_t{0}, gid_t{0});

    char target[] = "/etc/passwd";
    char path[]   = "/root/link";

    ASTIA_CHECK(submit_symlinkat(target, path) == 0);
}

ASTIA_TEST(aio_filesystem_utimensat_requires_owner_or_write) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_file("/tmp/victim", 0644, uid_t{0}, gid_t{0});

    char path[] = "/tmp/victim";

    ASTIA_CHECK(submit_utimensat(path) == -EACCES);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_utimensat_allows_owner) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_file("/tmp/owned", 0444, uid_t{1000}, gid_t{1000});

    char path[] = "/tmp/owned";

    ASTIA_CHECK(submit_utimensat(path) == 0);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_utimensat_allows_group_write) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_file("/tmp/shared", 0020, uid_t{0}, gid_t{1000});

    char path[] = "/tmp/shared";

    ASTIA_CHECK(submit_utimensat(path) == 0);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_utimensat_allows_root) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_file("/tmp/protected", 0444, uid_t{0}, gid_t{0});

    char path[] = "/tmp/protected";

    ASTIA_CHECK(submit_utimensat(path) == 0);
}

ASTIA_TEST(aio_filesystem_readdir_requires_read_permission) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_directory("/unreadable", 0711, uid_t{0}, gid_t{0});

    astia_dirent entry{};

    char path[] = "/unreadable";

    ASTIA_CHECK(submit_readdir_path(path, 0, &entry) == -EACCES);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_readdir_requires_search_permission) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_directory("/noexec", 0744, uid_t{0}, gid_t{0});

    astia_dirent entry{};

    char path[] = "/noexec";

    ASTIA_CHECK(submit_readdir_path(path, 0, &entry) == -EACCES);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_readdir_allows_listable_directory) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_directory("/shared", 0755, uid_t{0}, gid_t{0});

    astia_dirent entry{};

    char path[] = "/shared";

    ASTIA_CHECK(submit_readdir_path(path, 0, &entry) == 1);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_readdir_root_bypasses_permissions) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_directory("/root", 0700, uid_t{0}, gid_t{0});

    astia_dirent entry{};

    char path[] = "/root";

    ASTIA_CHECK(submit_readdir_path(path, 0, &entry) == 1);
}

ASTIA_TEST(aio_filesystem_statx_requires_search_on_parent) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_directory("/secret", 0700, uid_t{0}, gid_t{0});
    set_file("/secret/file", 0644, uid_t{0}, gid_t{0});

    astia_stat stat{};

    char path[] = "/secret/file";

    ASTIA_CHECK(submit_statx(path, -1, 0, &stat) == -EACCES);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_statx_allows_searchable_parent) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_process_credentials(uid_t{1000}, gid_t{1000});
    set_directory("/shared", 0755, uid_t{0}, gid_t{0});
    set_node_mode(fs::file_mode(fs::file_type::regular, fs::file_permissions(0644)));
    set_open_file(aio_test::make_open_file(new aio_test::fake_file));

    astia_stat stat{};

    char path[] = "/shared/file";

    ASTIA_CHECK(submit_statx(path, -1, 0, &stat) == 0);

    restore_credentials();
}

ASTIA_TEST(aio_filesystem_statx_root_bypasses_search) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_directory("/secret", 0700, uid_t{0}, gid_t{0});
    set_node_mode(fs::file_mode(fs::file_type::regular, fs::file_permissions(0644)));
    set_open_file(aio_test::make_open_file(new aio_test::fake_file));

    astia_stat stat{};

    char path[] = "/secret/file";

    ASTIA_CHECK(submit_statx(path, -1, 0, &stat) == 0);
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

ASTIA_TEST(aio_filesystem_statx_reports_directory_identity) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_node_mode(
        fs::file_mode(fs::file_type::directory, fs::default_directory_permissions));
    set_directory("/tmp/first", 0755, uid_t{0}, gid_t{0});
    set_node_identity("/tmp/first", 7, 4242);
    set_directory("/tmp/second", 0755, uid_t{0}, gid_t{0});
    set_node_identity("/tmp/second", 7, 9001);

    char first_path[]  = "/tmp/first";
    char second_path[] = "/tmp/second";

    astia_stat first{};
    astia_stat second{};

    ASTIA_CHECK(submit_statx(first_path, -1, 0, &first) == 0);
    ASTIA_CHECK(submit_statx(second_path, -1, 0, &second) == 0);
    ASTIA_CHECK(first.dev == 7);
    ASTIA_CHECK(first.ino == 4242);
    ASTIA_CHECK(second.dev == 7);
    ASTIA_CHECK(second.ino == 9001);
    ASTIA_CHECK(first.ino != second.ino);
}

ASTIA_TEST(aio_filesystem_statx_reports_implicit_directory_identity) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_node_identity("/tmp/implicit", 5, 123);

    char path[] = "/tmp/implicit";

    astia_stat stat{};

    ASTIA_CHECK(submit_statx(path, -1, 0, &stat) == 0);
    ASTIA_CHECK(stat.dev == 5);
    ASTIA_CHECK(stat.ino == 123);
}

ASTIA_TEST(aio_filesystem_statx_reports_symlink_identity) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_node_mode(fs::file_mode(fs::file_type::symlink, fs::default_symlink_permissions));
    set_node_identity("/tmp/link", 3, 77);

    char path[] = "/tmp/link";

    astia_stat stat{};

    ASTIA_CHECK(submit_statx(path, -1, STATX_NOFOLLOW, &stat) == 0);
    ASTIA_CHECK(stat.dev == 3);
    ASTIA_CHECK(stat.ino == 77);
}

ASTIA_TEST(aio_filesystem_statx_fstat_uses_handle_device) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_node_identity("/tmp/node", 9, 123);

    auto file = aio_test::make_open_file(new aio_test::fake_file);

    file->set_path("/tmp/node");

    const i32 fd = install_descriptor(file);

    astia_stat           stat{};
    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sqe = sqe_for(OP_STATX, fd);

    sqe.offset = reinterpret_cast<u64>(&stat);
    sqe.length = sizeof(stat);

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.result(0) == 0);
    ASTIA_CHECK(stat.dev == 9);
    ASTIA_CHECK(stat.ino == 123);
}

ASTIA_TEST(aio_filesystem_statx_reports_production_size) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();

    auto* node = new aio_test::fake_file;

    node->size_value          = 37;
    node->recorded_timestamps = fs::file_timestamps{11, 12, 21, 22, 31, 32};

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
    ASTIA_CHECK(stat.atime_sec == 11 && stat.atime_nsec == 12);
    ASTIA_CHECK(stat.mtime_sec == 21 && stat.mtime_nsec == 22);
    ASTIA_CHECK(stat.ctime_sec == 31 && stat.ctime_nsec == 32);
}

ASTIA_TEST(aio_filesystem_readdir_reports_posix_d_type_values) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();

    const i32 fd = install_descriptor(aio_test::make_open_file(new aio_test::fake_file));

    struct type_case {
        fs::file_type type;
        u32           d_type;
    };

    const type_case cases[] = {
        {fs::file_type::unknown, 0},   {fs::file_type::fifo, 1},
        {fs::file_type::character, 2}, {fs::file_type::directory, 4},
        {fs::file_type::block, 6},     {fs::file_type::regular, 8},
        {fs::file_type::symlink, 10},  {fs::file_type::socket, 12},
    };

    for (const type_case& item : cases) {
        set_node_mode(fs::file_mode(item.type, fs::default_file_permissions));

        astia_dirent entry{};

        ASTIA_CHECK(submit_readdir(fd, &entry, 0) == 1);
        ASTIA_CHECK(entry.type == item.d_type);
        ASTIA_CHECK(entry.namelen == 1);
        ASTIA_CHECK(entry.name[0] == 'x' && entry.name[1] == '\0');
    }
}

ASTIA_TEST(aio_filesystem_readdir_reports_entry_inode) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    set_directory_entry_inode(4242);

    const auto file =
        aio_test::make_open_file(new aio_test::fake_file(fs::file_type::directory));

    const i32 fd = install_descriptor(file);

    astia_dirent         entry{};
    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sqe = sqe_for(OP_READDIR, fd);

    sqe.address  = reinterpret_cast<u64>(&entry);
    sqe.op_flags = ASTIA_AIO_READDIR_FD;

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.result(0) == 1);
    ASTIA_CHECK(entry.ino == 4242);
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
