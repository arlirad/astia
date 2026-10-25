#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall::detail {
inline constexpr usize path_cap = 256;
inline constexpr usize arg_cap = 256;
inline constexpr usize vec_max = 32;
i64 copy_in_vector(char** vec, char* pool, u64 user_vector);
}
