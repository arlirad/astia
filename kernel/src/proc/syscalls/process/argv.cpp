#include "argv.hpp"
#include "user_mem.hpp"
#include <astia/api/errno.h>
namespace astia::proc::syscall::detail {
i64 copy_in_vector(char** vec, char* pool, u64 user_vector) {
    if (user_vector == 0) { vec[0] = nullptr; return 0; }
    for (usize i = 0; i < vec_max; ++i) {
        char* element = nullptr;
        const u64 slot = user_vector + i * sizeof(char*);
        if (slot < user_vector ||
            !sys::copy_in(&element, sys::user_range::bytes(slot, sizeof element)))
            return -EFAULT;
        if (element == nullptr) { vec[i] = nullptr; return 0; }
        if (!sys::copy_in_string(pool, arg_cap, sys::user_range::string(reinterpret_cast<u64>(element)))) return -EFAULT;
        vec[i] = pool; pool += arg_cap;
    }
    return -E2BIG;
}
}
