#pragma once

#if defined(__x86_64__)
#include "../../../src/arch/x86_64/user_layout.hpp"
namespace astia::arch {
    using user_layout_policy = x86_64::user_layout_policy;
}
#elif defined(__aarch64__)
#include "../../../src/arch/aarch64/user_layout.hpp"
#else
#error "Unsupported architecture: no user-layout policy is available"
#endif
