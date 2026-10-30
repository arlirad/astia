#include "getpgrp.hpp"

#include "../proc.hpp"
namespace astia::proc::syscall {
    i64 getpgrp() {
        return static_cast<i64>(static_cast<u32>(proc::getpgrp()));
    }
}
