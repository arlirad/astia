#include "argv.hpp"

#include "user_mem.hpp"
#include <astia/api/errno.h>
#include <astia/mm/heap.hpp>
namespace astia::proc::syscall::detail {
    argument_staging::~argument_staging() {
        mm::heap::free(storage_);
    }

    bool argument_staging::allocate() {
        const usize vec_bytes  = 2 * (vec_max + 1) * sizeof(char*);
        const usize pool_bytes = 2 * vec_max * arg_cap;
        auto        storage    = mm::heap::alloc(vec_bytes + pool_bytes);
        if (!storage)
            return false;

        storage_ = storage.value;
        argv_    = reinterpret_cast<char**>(storage_);
        envp_    = argv_ + vec_max + 1;
        pool_    = reinterpret_cast<char*>(envp_ + vec_max + 1);
        return true;
    }

    i64 argument_staging::copy(u64 argv, u64 envp) {
        i64 result = copy_in_vector(argv_, pool_, argv);
        if (result == 0)
            result = copy_in_vector(envp_, pool_ + vec_max * arg_cap, envp);
        return result;
    }

    i64 copy_in_vector(char** vec, char* pool, u64 user_vector) {
        if (user_vector == 0) {
            vec[0] = nullptr;
            return 0;
        }
        for (usize i = 0; i < vec_max; ++i) {
            char*     element = nullptr;
            const u64 delta = static_cast<u64>(i) * sizeof(char*);
            if (delta > ~u64{0} - user_vector)
                return -EFAULT;
            const auto slot = sys::user_ptr<const u64>(user_vector + delta).validate();
            if (!slot || !sys::copy_in(&element, slot.value.bytes()))
                return -EFAULT;
            if (element == nullptr) {
                vec[i] = nullptr;
                return 0;
            }
            if (!sys::copy_in_string(pool, arg_cap,
                                     sys::user_ptr<char>(reinterpret_cast<u64>(element))
                                         .string()))
                return -EFAULT;
            vec[i] = pool;
            pool += arg_cap;
        }
        return -E2BIG;
    }
}
