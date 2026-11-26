#pragma once

#include <astia/common.hpp>
#include <astia/core/task.hpp>
#include <astia/elf.hpp>
#include <astia/span.hpp>

#include "exports.hpp"
#include "types.hpp"

// Kernel module registry.
//
// A module is a relocatable x86-64 ELF object that exports a `module_main`
// entry point. The kernel loads it into frame-allocator memory, resolves its
// undefined symbols against the linked kernel symbol table,
// applies relocations, and calls `module_main`.
//
// This layer owns the policy (where memory comes from, what a symbol means);
// the ELF mechanics live in <astia/elf.hpp>.
//
// Stable handles
// --------------
// A registry slot is never compacted. A published module is addressed by a
// `module_id` of {slot, generation}; unloading a slot bumps its generation, so
// a stale id deterministically fails instead of aliasing the slot's next
// occupant. Raw `loaded_module` pointers never escape through the syscall-facing
// API below.
//
// Lifecycle shape
// ---------------
// `open()` / `close()` are the task-shaped lifecycle operations for syscall
// handlers. `open()` publishes only after the image is completely validated,
// relocated, and its dependencies are pinned; it performs no waiting.
// `close()` quiesces the module (new references and dependencies are refused),
// awaits active-reference and dependent drain through a `core::completion`,
// and performs the final unmap in the waiting task. A `close()` waiter is
// resumed through the resume queue, never from the releasing stack.
//
// `load_image`, `run`, `exit`, and the legacy pointer functions are synchronous
// adapters for boot and host tests. They never wait; a syscall path must use
// `open`/`close`.
//
// Locking
// -------
// Registry metadata is protected by one core::spinlock taken through
// arch::interrupt_spinlock_guard. The lock is never held across ELF load or
// relocation, finalizers, entrypoints, exit handlers, dependency callbacks, or
// address-space unmap work.
//
// Unload safety
// -------------
// A running module cannot be closed or unloaded: the driver must finish its
// remove path and call `exit` first, which is the existing driver/IRQ ownership
// contract. A resident module is permanently non-unloadable. Active references
// (callbacks/tasks) and dependents both block unload; the final unmap only
// happens once both counts are zero.

namespace astia::proc {
    class process;
}

namespace astia::mod {
    /// Stable, generation-checked module handle. `slot` indexes the fixed
    /// registry; `generation` changes on every slot release. Wraparound would
    /// require 2^64 releases of one slot, so a stale id cannot alias a later
    /// occupant in practice; the registry does not wrap or compact.
    struct module_id {
        u32 slot       = ~u32{0};
        u64 generation = 0;

        constexpr bool valid() const {
            return slot != ~u32{0};
        }

        constexpr explicit operator bool() const {
            return valid();
        }

        friend constexpr bool operator==(module_id, module_id) = default;
    };

    /// Registry slot lifecycle. `loading` and `unloading` are private
    /// bookkeeping states: a module id is not published until `loaded`.
    enum class module_state : u8 {
        free,
        loading,
        loaded,
        running,
        exiting,
        unloading,
    };

    /// Deterministic lifecycle failure reasons. `module_errno` maps each one
    /// to the negative ABI errno used by the future syscall layer.
    enum class module_error : int {
        ok = 0,
        invalid_argument,
        not_found,
        stale_id,
        no_space,
        no_memory,
        limit,
        load_failed,
        busy,
        would_block,
        running,
        resident,
        not_running,
        dependency,
        cycle,
        access,
        canceled,
    };

    /// Negative errno for `error`, or 0 for `module_error::ok`.
    int module_errno(module_error error) noexcept;

    /// Upper bound on simultaneously loaded modules. Fixed capacity keeps the
    /// registry off the heap.
    inline constexpr usize max_modules = 16;

    /// Upper bound on a module's dependency edges.
    inline constexpr usize max_module_dependencies = 8;

    /// Upper bound on a module's diagnostic name.
    inline constexpr usize max_module_name = 64;

    /// Upper bound on a userspace module image accepted by open_path().
    inline constexpr usize max_module_image_bytes = 1u << 20;

    /// Options for a transactional load.
    struct module_load_options {
        /// Boot-loaded modules stay resident and can never be unloaded.
        bool resident = false;
        /// Providers this module owns a reference on for its whole lifetime.
        span<const module_id> dependencies{};
        /// Bounded diagnostic name.  A zeroed name uses "module".
        char name[max_module_name] = {};
    };

    /// A loaded kernel module.
    struct loaded_module {
        struct exit_handler {
            void (*plain)(void) = nullptr;
            void (*cxa)(void*)  = nullptr;
            void* argument      = nullptr;
            void* dso           = nullptr;
            bool  removed       = false;
        };

        static constexpr usize max_exit_handlers = 64;

        module_id id{};                       ///< Stable registry handle.
        char      name[max_module_name] = {}; ///< Module name (for diagnostics).
        void*     base = nullptr;             ///< Load address of the image allocation.
        usize     size = 0;                   ///< Bytes allocated for the image.
        int (*entry)() = nullptr;             ///< Resolved module_main().
        bool                  running     = false; ///< True once entry() has been called.
        bool                  initialized = false;
        elf::loader::init_fn  init_array[elf::loader::max_init_array_entries] = {};
        usize                 init_count                                      = 0;
        elf::loader::init_fn  fini_array[elf::loader::max_init_array_entries] = {};
        usize                 fini_count                                      = 0;
        void*                 loader_context                                  = nullptr;
        elf::loader::unmap_fn loader_unmap                                    = nullptr;
        exit_handler          exit_handlers[max_exit_handlers]                = {};
        usize                 exit_count                                      = 0;
        void*                 dso_identity                                    = nullptr;

        u32       active_refs                           = 0;
        u32       dependent_count                       = 0;
        usize     dependency_count                      = 0;
        module_id dependencies[max_module_dependencies] = {};
        bool      resident                              = false;
        bool      quiescing                             = false;
        bool      entry_active                          = false;
        u32       callback_inflight                     = 0;
    };

    /// Bounded snapshot of one published module.
    struct module_info {
        module_id    id{};
        char         name[max_module_name] = {};
        module_state state                 = module_state::free;
        usize        image_size            = 0;
        u32          active_refs           = 0;
        usize        dependency_count      = 0;
        u32          dependent_count       = 0;
        bool         running               = false;
        bool         initialized           = false;
        bool         resident              = false;
        bool         quiescing             = false;
        bool         unloadable            = false;
    };

    /// Result of the task-shaped open operation.
    struct module_open_result {
        module_error error = module_error::invalid_argument;
        module_id    id{};

        constexpr bool ok() const {
            return error == module_error::ok;
        }
    };

    /// Transactionally load and publish a module from an image the caller
    /// already owns. The image is only read during this call; section contents
    /// are copied into kernel memory before publication. The image bytes must
    /// remain valid until the returned task completes; the environment and
    /// caller options are captured by value at call time, so a temporary
    /// `elf::loader::env` or `module_load_options` is safe.
    ///
    /// Task-shaped for the future modopen handler. This path currently performs
    /// no waiting: image validation, relocation, and dependency pinning all
    /// complete before the first suspension point that a streaming image source
    /// can introduce.
    core::task<module_open_result> open(span<const u8> image, const elf::loader::env& env,
                                        const module_load_options& options = {});
    core::task<module_open_result> open(span<const u8>             image,
                                        const module_load_options& options = {});

    /// Resolve `path` in `caller`'s VFS namespace, verify read permission and
    /// a regular-file ELF image, read the bounded image, and transactionally
    /// load it.  Resolution, permission, size, format, and dependency checks
    /// all precede publication; failure leaves no visible slot.  Task-shaped
    /// because filesystem reads can wait.
    core::task<module_open_result> open_path(proc::process& caller, const char* path,
                                             const module_load_options& options);

    /// Validate the kernel-module ABI image header.  Only ET_REL relocatable
    /// objects are modules; ET_EXEC/ET_DYN images are userspace executables
    /// and are rejected before the generic loader can publish them.
    inline module_error validate_module_image_format(span<const u8> image) {
        if (image.data() == nullptr || image.size() == 0)
            return module_error::invalid_argument;

        if (elf::loader::kind_of(image) != elf::loader::kind::relocatable)
            return module_error::load_failed;

        return module_error::ok;
    }

    /// Quiesce and unload a module, awaiting active-reference and dependent
    /// drain. Registered close waiters resume through the resume queue; the
    /// final unmap runs in the waiting task, never under the registry lock.
    /// A close owns the slot until it unloads or returns, so another unload
    /// cannot bypass a pending close.
    ///
    /// Rejects a running module with `module_error::running` (driver teardown
    /// and `exit` must happen first) and a resident module with
    /// `module_error::resident`.
    core::task<module_error> close(module_id id);

    /// Synchronous transactional load adapter for boot and host tests.
    /// Syscall handlers must use `open`. Returns `module_error::ok` and a
    /// published id on success.
    module_error load_image(span<const u8> image, const module_load_options& options,
                            module_id& out);
    module_error load_image(span<const u8> image, const elf::loader::env& env,
                            const module_load_options& options, module_id& out);

    /// Synchronous entrypoint runner. Existing entrypoint semantics are
    /// unchanged: init arrays run in order, then `module_main`. Execution and
    /// finalization are serialised through a single execution-context gate so
    /// the atexit/CXA callback owner cannot be overwritten by a concurrent
    /// run/exit; a concurrent attempt returns `busy`.
    module_error run(module_id id, int& result);

    /// Run finalizers and exit handlers, leaving the module loaded (and, for a
    /// resident module, permanently non-unloadable). Shares the execution gate
    /// with `run`.
    module_error exit(module_id id);

    /// Strict synchronous unload. Rejects running/resident modules, active
    /// references, and dependents. Never waits; use `close` on a syscall path.
    module_error unload(module_id id);

    /// Cancel a pending quiescing close, waking its waiter with
    /// `module_error::canceled`.
    module_error cancel_close(module_id id);

    /// Nonblocking lookup. Copies bounded metadata; returns `busy` while a slot
    /// is loading/unloading and `stale_id` for a released generation.
    module_error lookup(module_id id, module_info& out);

    inline module_error stat(module_id id, module_info& out) {
        return lookup(id, out);
    }

    /// Consistent, nonblocking snapshot of published slots in slot order.
    /// Returns the number of entries written (truncated to `out.size()`).
    /// When `total` is non-null it receives the published count visible to
    /// the same snapshot, even when `out` is smaller or empty.
    usize snapshot(span<module_info> out, usize* total = nullptr);

    /// One-lock snapshot of a module plus its bounded dependency ids.
    /// `dependency_total` receives the stored edge count regardless of the
    /// output span size.  Returns `busy` for an unpublished slot.
    module_error snapshot_module(module_id id, module_info& out,
                                 span<module_id> dependencies, usize& dependency_total);

    /// Bounded dependency enumeration for a module.
    module_error dependency_at(module_id id, usize index, module_id& out);

    /// Add/remove a lifetime dependency edge. A dependent blocks its provider's
    /// unload; cycles and self-edges are rejected.
    module_error add_dependency(module_id dependent, module_id dependency);
    module_error remove_dependency(module_id dependent, module_id dependency);

    /// Active-reference hooks for callbacks/tasks. Acquire fails with
    /// `would_block` once a close has quiesced the module.
    ///
    /// These are accounting primitives only: they do not discover, register,
    /// or teardown IRQ handlers, and they do not integrate with task lifetime
    /// automatically. A driver/consumer must bracket every code path that can
    /// execute module text with acquire/release, and finish IRQ/driver remove
    /// before `exit`; otherwise unload can only see the running bit and cannot
    /// prove quiescence.
    module_error acquire(module_id id);
    module_error release(module_id id);

    /// Deliver close waiters that were parked because release() ran in
    /// interrupt or module-callback context. Call from normal context; the
    /// scheduler invokes it each tick.
    void service_module_waits();

    /// The module currently executing entrypoint or finalizer callbacks, or an
    /// invalid id outside that context. Used to resolve the calling module for
    /// lifetime_token.
    module_id active_module_id();

    /// True when the module is loaded, not running, and has no active refs.
    bool quiesced(module_id id);

    /// Permanently mark a module non-unloadable (boot policy). There is no
    /// production operation that clears the flag.
    module_error make_resident(module_id id);

#ifdef ASTIA_MODULE_HOST_TEST
    /// Host-test-only teardown of a resident fixture. Not part of the kernel
    /// contract and never compiled into a production build.
    module_error clear_resident_for_test(module_id id);
#endif

    // --- Legacy synchronous adapters -------------------------------------
    //
    // These predate the stable-id API and remain for boot and the existing
    // host lifecycle fixtures. Returned pointers are registry addresses,
    // invalid after unload; syscall code must not receive them.

    const loaded_module* load_module(span<const u8> image);
    const loaded_module* load_module(span<const u8> image, const elf::loader::env& env);

    inline const loaded_module* load_module(const void* image, usize size) {
        return load_module(span<const u8>(static_cast<const u8*>(image), size));
    }

    bool unload_module(const loaded_module* mod);

    bool exit_module(const loaded_module* mod);

    int  register_exit_handler(void (*handler)(void));
    int  register_exit_handler(void (*handler)(void*), void* argument, void* dso);
    void finalize_exit_handlers(void* dso);

    /// Call a loaded module's entry point. Returns the module's return code, or
    /// -1 if `mod` is null or not in the loaded state.
    int run_module(const loaded_module* mod);

    /// Number of published (loaded, running, or exiting) modules.
    usize count();

    /// The i-th published module in slot order, or null if out of range.
    const loaded_module* at(usize i);
} // namespace astia::mod
