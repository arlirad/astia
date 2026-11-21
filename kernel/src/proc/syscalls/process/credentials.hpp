#pragma once

#include <astia/common.hpp>

namespace astia::proc::syscall {
    i64 getuid();
    i64 geteuid();
    i64 getgid();
    i64 getegid();
    i64 setuid(i64 value);
    i64 seteuid(i64 value);
    i64 setgid(i64 value);
    i64 setegid(i64 value);
    i64 setreuid(i64 real, i64 effective);
    i64 setregid(i64 real, i64 effective);
    i64 getgroups(i64 count, i64 output);
    i64 setgroups(i64 count, i64 input);
}
