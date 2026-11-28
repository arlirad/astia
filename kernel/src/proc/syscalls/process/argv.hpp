#pragma once
#include <astia/common.hpp>
#include <astia/resource_limits.hpp>
#include <astia/span.hpp>
namespace astia::proc::syscalls::detail {
    inline constexpr usz arg_cap = resource_limits::policy::spawn_string_bytes;
    inline constexpr usz vec_max =
        resource_limits::policy::argv_entries > resource_limits::policy::env_entries
            ? resource_limits::policy::argv_entries
            : resource_limits::policy::env_entries;
    inline constexpr usz pool_cap       = resource_limits::policy::spawn_argument_bytes;
    inline constexpr usz chunk_cap      = 4 * arg_cap;
    inline constexpr usz pool_chunk_max = pool_cap / (chunk_cap - arg_cap) + 2;

    // Owns the complete argv/envp staging block.  Strings live in
    // fixed-size chunks rather than one ARG_MAX-sized block, so a normal
    // exec only allocates what it uses and never needs a large contiguous
    // allocation under heap pressure.
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
        bool add_chunk();
        i64  copy_vector(char** vec, u64 user_vector);

        u8*    _storage = nullptr;
        char** _argv    = nullptr;
        char** _envp    = nullptr;
        char*  _chunks[pool_chunk_max]{};
        usz    _chunk_count = 0;
        char*  _chunk       = nullptr;
        usz    _chunk_used  = 0;
        usz    _pool_used   = 0;
    };
}
