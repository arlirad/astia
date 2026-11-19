#pragma once
#include <astia/boot/types.hpp>
#include <astia/common.hpp>

namespace astia::init {

    enum class phase : u8 { early, memory, devices, subsystems, late };

    struct context;

    using step_fn = bool (*)(context&);

    struct step {
        const char*        name;
        phase              level;
        step_fn            fn;
        const char* const* needs;
        const char* const* after;
        bool               optional;
    };

    struct context {
        /// Raw loader handoff: physical address of the protocol's start info.
        /// Read by the boot step only; everyone else uses `info`.
        const void* start_info = nullptr;

        /// Typed, bootloader-agnostic handoff, set by the boot step. Stays valid
        /// for the life of the kernel (see boot::boot_info).
        const boot::boot_info* info = nullptr;
    };

    bool run_steps(const step* first, const step* last, context& ctx);
    bool run(context& ctx);

} // namespace astia::init

#define ASTIA_INIT_STEP_BASE(phase_, name, fn, needs_arr, after_arr, opt_)                        \
    static const char* const* const  astia_init_needs_ptr_##name = needs_arr;                     \
    static const char* const* const  astia_init_after_ptr_##name = after_arr;                     \
    static const ::astia::init::step astia_init_step_##name __attribute__((                       \
        used, section(".initcall"), aligned(8)))                 = {#name,                        \
                                                                    ::astia::init::phase::phase_, \
                                                                    fn,                           \
                                                                    astia_init_needs_ptr_##name,  \
                                                                    astia_init_after_ptr_##name,  \
                                                                    opt_};

#define ASTIA_INIT_STEP_0(phase_, name, fn, d1, d2, d3)                    \
    static const char* const        astia_init_needs_##name[] = {nullptr}; \
    static const char* const* const astia_init_after_##name   = nullptr;   \
    ASTIA_INIT_STEP_BASE(phase_, name, fn, astia_init_needs_##name,        \
                         astia_init_after_##name, false)

#define ASTIA_INIT_STEP_1(phase_, name, fn, dep1, d2, d3)                        \
    static const char* const        astia_init_needs_##name[] = {dep1, nullptr}; \
    static const char* const* const astia_init_after_##name   = nullptr;         \
    ASTIA_INIT_STEP_BASE(phase_, name, fn, astia_init_needs_##name,              \
                         astia_init_after_##name, false)

#define ASTIA_INIT_STEP_2(phase_, name, fn, dep1, dep2, d3)                            \
    static const char* const        astia_init_needs_##name[] = {dep1, dep2, nullptr}; \
    static const char* const* const astia_init_after_##name   = nullptr;               \
    ASTIA_INIT_STEP_BASE(phase_, name, fn, astia_init_needs_##name,                    \
                         astia_init_after_##name, false)

#define ASTIA_INIT_STEP_3(phase_, name, fn, dep1, dep2, dep3)                              \
    static const char* const astia_init_needs_##name[]      = {dep1, dep2, dep3, nullptr}; \
    static const char* const* const astia_init_after_##name = nullptr;                     \
    ASTIA_INIT_STEP_BASE(phase_, name, fn, astia_init_needs_##name,                        \
                         astia_init_after_##name, false)

#define ASTIA_INIT_STEP__(phase_, name, fn, d1, d2, d3, cnt, ...) \
    ASTIA_INIT_STEP_##cnt(phase_, name, fn, d1, d2, d3)

#define ASTIA_INIT_STEP(phase_, name, fn, ...) \
    ASTIA_INIT_STEP__(phase_, name, fn __VA_OPT__(, ) __VA_ARGS__, 3, 2, 1, 0)
