#include "argv.hpp"

#include <astia/api/errno.h>
#include <astia/mm/heap.hpp>

#include "user_mem.hpp"
namespace astia::proc::syscalls::detail {
    argument_staging::~argument_staging() {
        for (usz i = 0; i < _chunk_count; ++i) {
            mm::heap::free(_chunks[i]);
        }

        mm::heap::free(_storage);
    }

    bool argument_staging::allocate() {
        const usz vec_bytes = 2 * (vec_max + 1) * sizeof(char*);

        auto storage = mm::heap::alloc_zeroed(vec_bytes);
        if (!storage)
            return false;

        _storage = storage.value;
        _argv    = reinterpret_cast<char**>(_storage);
        _envp    = _argv + vec_max + 1;

        return true;
    }

    bool argument_staging::add_chunk() {
        if (_chunk_count == pool_chunk_max)
            return false;

        auto storage = mm::heap::alloc(chunk_cap);
        if (!storage)
            return false;

        _chunk                  = reinterpret_cast<char*>(storage.value);
        _chunks[_chunk_count++] = _chunk;
        _chunk_used             = 0;

        return true;
    }

    i64 argument_staging::copy(u64 argv, u64 envp) {
        const i64 result = copy_vector(_argv, argv);
        if (result != 0)
            return result;

        return copy_vector(_envp, envp);
    }

    i64 argument_staging::copy_vector(char** vec, u64 user_vector) {
        if (user_vector == 0) {
            vec[0] = nullptr;

            return 0;
        }

        for (usz i = 0; i < vec_max; ++i) {
            char* element = nullptr;

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

            const usz budget_left = pool_cap - _pool_used;
            if (budget_left == 0)
                return -E2BIG;

            const usz limit = budget_left < arg_cap ? budget_left : arg_cap;

            if (_chunk == nullptr && !add_chunk())
                return -ENOMEM;

            if (_chunk_used == chunk_cap && !add_chunk())
                return -ENOMEM;

            usz tail   = chunk_cap - _chunk_used;
            usz take   = tail < limit ? tail : limit;
            usz length = 0;

            auto copied = sys::copy_in_string_ex(
                span<char>(_chunk + _chunk_used, take),
                sys::user_ptr<char>(reinterpret_cast<u64>(element)).string(), length);

            if (copied == sys::string_copy::too_long && take < limit) {
                if (!add_chunk())
                    return -ENOMEM;

                copied = sys::copy_in_string_ex(
                    span<char>(_chunk, limit),
                    sys::user_ptr<char>(reinterpret_cast<u64>(element)).string(), length);
            }

            if (copied == sys::string_copy::fault)
                return -EFAULT;

            if (copied == sys::string_copy::too_long)
                return -E2BIG;

            vec[i] = _chunk + _chunk_used;
            _chunk_used += length;
            _pool_used += length;
        }

        return -E2BIG;
    }
}
