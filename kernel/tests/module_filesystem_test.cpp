#include "module_filesystem.hpp"
#include "test.hpp"

#include <astia/api/errno.h>
#include <cstring>

ASTIA_TEST(module_filesystem_registration_lifecycle) {
    using astia::fs::module_filesystem_registration;

    module_filesystem_registration registration;
    ASTIA_CHECK(!registration.valid());
    ASTIA_CHECK(astia::fs::register_module_filesystem("ext4", registration) == 0);
    ASTIA_CHECK(registration.valid());
    ASTIA_CHECK(astia::fs::module_filesystem_count() == 1);
    ASTIA_CHECK(astia::fs::register_module_filesystem("ext4", registration) == -EINVAL);

    module_filesystem_registration stale = registration;
    ASTIA_CHECK(astia::fs::unregister_module_filesystem(registration) == 0);
    ASTIA_CHECK(!registration.valid());
    ASTIA_CHECK(astia::fs::module_filesystem_count() == 0);
    ASTIA_CHECK(astia::fs::unregister_module_filesystem(stale) == -ENOENT);
}

ASTIA_TEST(module_filesystem_registration_rejects_invalid_names) {
    astia::fs::module_filesystem_registration registration;
    ASTIA_CHECK(astia::fs::register_module_filesystem(nullptr, registration) == -EINVAL);
    ASTIA_CHECK(astia::fs::register_module_filesystem("", registration) == -EINVAL);
    char too_long[astia::fs::module_filesystem_name_max + 1]{};
    std::memset(too_long, 'x', sizeof too_long - 1);
    ASTIA_CHECK(astia::fs::register_module_filesystem(too_long, registration) == -ENFILE);
}
