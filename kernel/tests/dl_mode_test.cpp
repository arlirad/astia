#include "../../dl/mode.h"
#include "support/test.hpp"

#include <dlfcn.h>

ASTIA_TEST(dl_mode_accepts_supported_combinations) {
    ASTIA_CHECK(ld_mode_valid(RTLD_LOCAL) == 0);
    ASTIA_CHECK(ld_mode_valid(RTLD_LAZY) == 0);
    ASTIA_CHECK(ld_mode_valid(RTLD_NOW) == 0);
    ASTIA_CHECK(ld_mode_valid(RTLD_GLOBAL) == 0);
    ASTIA_CHECK(ld_mode_valid(RTLD_NODELETE) == 0);
    ASTIA_CHECK(ld_mode_valid(RTLD_LAZY | RTLD_GLOBAL) == 0);
    ASTIA_CHECK(ld_mode_valid(RTLD_NOW | RTLD_GLOBAL) == 0);
    ASTIA_CHECK(ld_mode_valid(RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE) == 0);
}

ASTIA_TEST(dl_mode_rejects_conflicting_binding) {
    ASTIA_CHECK(ld_mode_valid(RTLD_LAZY | RTLD_NOW) == -1);
    ASTIA_CHECK(ld_mode_valid(RTLD_LAZY | RTLD_NOW | RTLD_GLOBAL) == -1);
    ASTIA_CHECK(ld_mode_valid(RTLD_LAZY | RTLD_NOW | RTLD_NODELETE) == -1);
}

ASTIA_TEST(dl_mode_rejects_unsupported_flags) {
    ASTIA_CHECK(ld_mode_valid(RTLD_NOLOAD) == -1);
    ASTIA_CHECK(ld_mode_valid(RTLD_NOW | RTLD_NOLOAD) == -1);
    ASTIA_CHECK(ld_mode_valid(0x40000000) == -1);
    ASTIA_CHECK(ld_mode_valid(-1) == -1);
}
