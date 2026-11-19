#include "aio_filesystem.hpp"
#include "support/test.hpp"

ASTIA_TEST(aio_filesystem_family_owns_path_file_and_directory_operations) {
    const unsigned operations[] = {
        OP_OPENAT,    OP_CLOSE,      OP_STATX,   OP_FSYNC,  OP_UNLINKAT,
        OP_MKDIRAT,   OP_RENAMEAT,   OP_READDIR, OP_CHMOD,  OP_UTIMENSAT,
        OP_SYMLINKAT, OP_READLINKAT, OP_CHOWN,   OP_LINKAT, OP_FTRUNCATE,
    };
    for (unsigned operation : operations)
        ASTIA_CHECK(astia::sys::aio_filesystem::handles(operation));
}

ASTIA_TEST(aio_filesystem_dispatch_boundaries_stay_in_orchestration) {
    ASTIA_CHECK(!astia::sys::aio_filesystem::handles(OP_NOP));
    ASTIA_CHECK(!astia::sys::aio_filesystem::handles(OP_READ));
    ASTIA_CHECK(!astia::sys::aio_filesystem::handles(OP_WRITE));
    ASTIA_CHECK(!astia::sys::aio_filesystem::handles(OP_POLL_ADD));
    ASTIA_CHECK(!astia::sys::aio_filesystem::handles(OP_CANCEL));
    ASTIA_CHECK(!astia::sys::aio_filesystem::handles(~0u));
}
