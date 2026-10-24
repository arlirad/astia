#include "setsid.hpp"
#include "../proc.hpp"
namespace astia::proc::syscall {
    i64 setsid() { return proc::setsid(); }
}
