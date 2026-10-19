#include "support/test.hpp"
#include <astia/api/syscalls.h>

ASTIA_TEST(process_group_syscalls_are_append_only) {
    ASTIA_CHECK(SYS_KILL == 20);
    ASTIA_CHECK(SYS_SIGSUSPEND == 25);
    ASTIA_CHECK(SYS_GETPGRP == 26);
    ASTIA_CHECK(SYS_SETPGID == 27);
    ASTIA_CHECK(SYS_SETSID == 28);
}
