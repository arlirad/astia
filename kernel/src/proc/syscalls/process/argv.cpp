#include "argv.hpp"
#include "user_mem.hpp"
#include <astia/api/errno.h>
namespace astia::proc::syscall::detail {
i64 copy_in_vector(char** vec, char* pool, u64 user_vector) {
    const sys::user_range vector = sys::user_range::bytes(user_vector, vec_max * sizeof(char*));
    if (vector.null()) { vec[0] = nullptr; return 0; }
    for (usize i = 0; i < vec_max; ++i) {
        char* element = nullptr;
        if (!sys::copy_in(&element, vector.slice(i * sizeof(char*), sizeof element))) return -EFAULT;
        if (element == nullptr) { vec[i] = nullptr; return 0; }
        if (!sys::copy_in_string(pool, arg_cap, sys::user_range::string(reinterpret_cast<u64>(element)))) return -EFAULT;
        vec[i] = pool; pool += arg_cap;
    }
    return -E2BIG;
}
}
