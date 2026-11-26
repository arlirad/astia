#pragma once
#include <astia/common.hpp>
#include <astia/span.hpp>
namespace astia::proc::syscalls::detail {
    inline constexpr usize path_cap = 256;
    inline constexpr usize arg_cap  = 256;
    inline constexpr usize vec_max  = 32;

    // Owns the complete argv/envp staging block.  Keeping the vectors and
    // their string pools together preserves the loader-facing layout while
    // making release unconditional on every syscall return path.
    class argument_staging {
        public:
        argument_staging() = default;
        ~argument_staging();

        argument_staging(const argument_staging&)            = delete;
        argument_staging& operator=(const argument_staging&) = delete;

        bool allocate();
        i64  copy(u64 argv, u64 envp);

        char** argv() const {
            return _argv;
        }

        char** envp() const {
            return _envp;
        }

        private:
        u8*    _storage = nullptr;
        char** _argv    = nullptr;
        char** _envp    = nullptr;
        char*  _pool    = nullptr;
    };

    i64 copy_in_vector(char** vec, span<char> pool, u64 user_vector);
}
