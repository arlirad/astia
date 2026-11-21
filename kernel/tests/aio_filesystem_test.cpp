#include "aio_registry_test_support.hpp"
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

ASTIA_TEST(aio_filesystem_family_owns_path_file_and_directory_operations) {
    using astia::io::aio::test_support::owner;
    using astia::io::aio::test_support::routes_to;

    const unsigned operations[] = {
        OP_OPENAT,    OP_CLOSE,      OP_STATX,   OP_FSYNC,  OP_UNLINKAT,
        OP_MKDIRAT,   OP_RENAMEAT,   OP_READDIR, OP_CHMOD,  OP_UTIMENSAT,
        OP_SYMLINKAT, OP_READLINKAT, OP_CHOWN,   OP_LINKAT, OP_FTRUNCATE,
    };
    for (unsigned operation : operations)
        ASTIA_CHECK(routes_to(owner::filesystem, operation));
}

ASTIA_TEST(aio_filesystem_dispatch_boundaries_stay_in_orchestration) {
    using astia::io::aio::test_support::owner;
    using astia::io::aio::test_support::routes_to;

    ASTIA_CHECK(!routes_to(owner::filesystem, OP_NOP));
    ASTIA_CHECK(!routes_to(owner::filesystem, OP_READ));
    ASTIA_CHECK(!routes_to(owner::filesystem, OP_WRITE));
    ASTIA_CHECK(!routes_to(owner::filesystem, OP_POLL_ADD));
    ASTIA_CHECK(!routes_to(owner::filesystem, OP_CANCEL));
    ASTIA_CHECK(!routes_to(owner::filesystem, ~0u));
}
