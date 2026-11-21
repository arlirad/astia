#pragma once
#include <astia/arch/context.hpp>
#include <astia/common.hpp>
namespace astia::proc::syscall {
    i64 sigreturn(const arch::syscall_frame& input);
}
