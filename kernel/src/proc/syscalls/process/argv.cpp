#include "argv.hpp"

#include <astia/api/errno.h>
#include <astia/mm/heap.hpp>

#include "user_mem.hpp"
namespace astia::proc::syscalls::detail {
    argument_staging::~argument_staging() {
        mm::heap::free(_storage);
    }

    bool argument_staging::allocate() {
        const usize vec_bytes  = 2 * (vec_max + 1) * sizeof(char*);
        const usize pool_bytes = 2 * vec_max * arg_cap;
        auto        storage    = mm::heap::alloc(vec_bytes + pool_bytes);
        if (!storage)
            return false;

        _storage = storage.value;
        _argv    = reinterpret_cast<char**>(_storage);
        _envp    = _argv + vec_max + 1;
        _pool    = reinterpret_cast<char*>(_envp + vec_max + 1);
        return true;
    }

    i64 argument_staging::copy(u64 argv, u64 envp) {
        i64 result = copy_in_vector(_argv, span<char>(_pool, vec_max * arg_cap), argv);
        if (result == 0)
            result = copy_in_vector(
                _envp, span<char>(_pool + vec_max * arg_cap, vec_max * arg_cap), envp);
        return result;
    }

    i64 copy_in_vector(char** vec, span<char> pool, u64 user_vector) {
        if (user_vector == 0) {
            vec[0] = nullptr;
            return 0;
        }
        for (usize i = 0; i < vec_max; ++i) {
            char*     element = nullptr;
            const u64 delta   = static_cast<u64>(i) * sizeof(char*);
            if (delta > ~u64{0} - user_vector)
                return -EFAULT;
            const auto slot = sys::user_ptr<const u64>(user_vector + delta).validate();
            if (!slot || !sys::copy_in(&element, slot.value.bytes()))
                return -EFAULT;
            if (element == nullptr) {
                vec[i] = nullptr;
                return 0;
            }
            if (!sys::copy_in_string(span<char>(pool.data(), arg_cap),
                                     sys::user_ptr<char>(reinterpret_cast<u64>(element))))
                return -EFAULT;
            vec[i] = pool.data();
            pool   = span<char>(pool.data() + arg_cap, pool.size() - arg_cap);
        }
        return -E2BIG;
    }
}
