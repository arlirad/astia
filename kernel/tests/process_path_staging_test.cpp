/*
 * execve/spawnve stage the image pathname at the VFS limit, not in a
 * quarter-kilobyte buffer.  Pinned here:
 *
 *   - a path longer than the old 256-byte buffer is staged intact;
 *   - the longest representable path (PATH_MAX - 1 characters) is accepted;
 *   - a path that does not terminate within PATH_MAX is ENAMETOOLONG, the
 *     errno chdir/chroot already report for the same input;
 *   - an unreadable pointer is still EFAULT, not ENAMETOOLONG.
 *
 * Real: path_staging and the user_mem.hpp copy/validate helpers.  Stubbed:
 * current_process(), which is NULL, so the kernel-client branch treats host
 * pointers as valid kernel memory and never touches a page map.
 */

#include <astia/api/errno.h>
#include <astia/fs/types.hpp>

#include "../src/proc/syscalls/process/path_staging.hpp"
#include "support/test.hpp"

namespace {
    using astia::proc::syscalls::detail::path_staging;
    using astia::sys::user_ptr;

    user_ptr<char> path_at(const char* address) {
        return user_ptr<char>(
            static_cast<astia::u64>(reinterpret_cast<astia::usz>(address)));
    }

    bool staged_equals(const path_staging& staged, const char* path) {
        for (astia::usz i = 0;; ++i) {
            if (staged.c_str()[i] != path[i])
                return false;

            if (path[i] == '\0')
                return true;
        }
    }
} // namespace

namespace astia::proc {
    process* current_process() {
        return nullptr;
    }
} // namespace astia::proc

namespace astia::mm {
    bool address_space::attached() const {
        return false;
    }

    bool address_space::valid_user_range(astia::u64, astia::usz) const {
        return false;
    }

    bool address_space::validate(astia::u64, astia::usz, access) const {
        return false;
    }

    bool address_space::user_accessible(astia::vaddr_t) const {
        return false;
    }
} // namespace astia::mm

namespace astia::proc {
    bool process::cow_fault(astia::u64, bool) {
        return false;
    }
} // namespace astia::proc

ASTIA_TEST(path_staging_accepts_a_path_longer_than_the_old_buffer) {
    char path[301];

    for (astia::usz i = 0; i < sizeof path - 1; ++i) {
        path[i] = static_cast<char>('a' + i % 26);
    }

    path[sizeof path - 1] = '\0';

    path_staging staged(path_at(path));

    ASTIA_CHECK(staged.copy() == 0);
    ASTIA_CHECK(staged_equals(staged, path));
}

ASTIA_TEST(path_staging_accepts_the_longest_path) {
    char path[astia::fs::path_max];

    for (astia::usz i = 0; i < sizeof path - 1; ++i) {
        path[i] = 'x';
    }

    path[sizeof path - 1] = '\0';

    path_staging staged(path_at(path));

    ASTIA_CHECK(staged.copy() == 0);
    ASTIA_CHECK(staged.c_str()[sizeof path - 2] == 'x');
    ASTIA_CHECK(staged.c_str()[sizeof path - 1] == '\0');
}

ASTIA_TEST(path_staging_rejects_a_path_beyond_path_max) {
    char path[astia::fs::path_max + 2];

    for (astia::usz i = 0; i < sizeof path - 1; ++i) {
        path[i] = 'x';
    }

    path[sizeof path - 1] = '\0';

    path_staging staged(path_at(path));

    ASTIA_CHECK(staged.copy() == -ENAMETOOLONG);
}

ASTIA_TEST(path_staging_reports_an_unreadable_pointer_as_efault) {
    path_staging staged(path_at(nullptr));

    ASTIA_CHECK(staged.copy() == -EFAULT);
}
