// Module registry: the policy layer over the pure ELF loader.
//
// Memory comes from the kernel frame allocator; undefined symbols resolve
// against the linked kernel symbol table. Both are wired in here as plain
// callbacks — no dependency injection, the loader just takes function pointers.

#include "module.hpp"

#include <astia/api/errno.h>
#include <astia/arch/spinlock_guard.hpp>
#include <astia/core/completion.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/elf.hpp>
#include <astia/mm/frame_allocator.hpp>

#include <new>

#if defined(ASTIA_MODULE_HOST_TEST)
#include <pthread.h>
#endif

#include "exports.hpp"

namespace astia::proc {
    class thread;
    thread* current_thread();
} // namespace astia::proc

namespace astia::mod {

#ifdef ASTIA_MODULE_HOST_TEST
    namespace host_test {
        int  invoke_entry(int (*entry)());
        void invoke_initializer(elf::loader::init_fn initializer);
        void invoke_finalizer(elf::loader::init_fn finalizer);
    }
#endif

    namespace {

        static_assert(max_modules <= 32, "dependency cycle bitmap covers max_modules");

        /// Refcounted close waiter. One reference belongs to the close
        /// coroutine and one to the slot registration, so a parked task may be
        /// destroyed while a wake delivery is in flight without freeing the
        /// completion under the deliverer.
        struct close_waiter {
            core::completion<module_error> completion{};
            u32                            refs = 1;
        };

        struct registry_slot {
            loaded_module module{};
            u64           generation  = 0;
            module_state  state       = module_state::free;
            close_waiter* close_wait  = nullptr;
            bool          close_owner = false;
        };

        struct load_state {
            void* dso = nullptr;
        };

        struct unload_plan {
            usize                 index = 0;
            module_id             id{};
            void*                 base               = nullptr;
            usize                 size               = 0;
            void*                 context            = nullptr;
            elf::loader::unmap_fn unmap              = nullptr;
            close_waiter*         wakes[max_modules] = {};
            usize                 wake_count         = 0;
        };

        registry_slot  slots[max_modules] = {};
        core::spinlock registry_lock;
        core::spinlock callback_lock;
        loaded_module* active          = nullptr;
        void*          callback_owner  = nullptr;
        u32            execution_owner = 0;

        void* callback_context_owner() {
#if defined(ASTIA_MODULE_HOST_TEST)
            return reinterpret_cast<void*>(static_cast<u64>(pthread_self()));
#else
            return static_cast<void*>(astia::proc::current_thread());
#endif
        }

        bool owns_callback_context_locked() {
            return active != nullptr && callback_owner == callback_context_owner();
        }

        void retain_waiter(close_waiter* waiter) {
            __atomic_add_fetch(&waiter->refs, 1, __ATOMIC_ACQ_REL);
        }

        void release_waiter(close_waiter* waiter) {
            if (waiter == nullptr) {
                return;
            }
            if (__atomic_sub_fetch(&waiter->refs, 1, __ATOMIC_ACQ_REL) == 0) {
                delete waiter;
            }
        }

        void deliver_waiter(close_waiter* waiter, module_error result) {
            if (waiter == nullptr) {
                return;
            }
            waiter->completion.complete_deferred(result);
            release_waiter(waiter);
        }

        bool begin_execution() {
            u32 expected = 0;
            return __atomic_compare_exchange_n(&execution_owner, &expected, 1, false,
                                               __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
        }

        void end_execution() {
            __atomic_store_n(&execution_owner, 0, __ATOMIC_RELEASE);
        }

        /// Serialises module entry/exit and finalizer callbacks. `active` is the
        /// callback-ownership anchor used by the C atexit shims, so exactly one
        /// execution may own it at a time; a concurrent or nested attempt fails
        /// with busy instead of cross-wiring handlers.
        class execution_guard {
            public:
            execution_guard() : _owned(begin_execution()) {}

            ~execution_guard() {
                if (_owned) {
                    end_execution();
                }
            }

            execution_guard(const execution_guard&)            = delete;
            execution_guard& operator=(const execution_guard&) = delete;

            bool owned() const {
                return _owned;
            }

            private:
            bool _owned;
        };

        bool published(module_state state) {
            return state == module_state::loaded || state == module_state::running ||
                   state == module_state::exiting;
        }

        bool name_equals(const char* left, const char* right) {
            while (*left != '\0' && *left == *right) {
                ++left;
                ++right;
            }
            return *left == *right;
        }

        void copy_module_name(char (&destination)[max_module_name],
                              const char (&source)[max_module_name]) {
            usize i = 0;
            while (i + 1 < max_module_name && source[i] != '\0') {
                destination[i] = source[i];
                ++i;
            }
            destination[i] = '\0';
        }

        registry_slot* find_slot_locked(module_id id, module_error& error) {
            error = module_error::not_found;
            if (!id.valid() || id.slot >= max_modules) {
                return nullptr;
            }
            registry_slot& slot = slots[id.slot];
            if (slot.generation != id.generation) {
                error = module_error::stale_id;
                return nullptr;
            }
            if (slot.state == module_state::free) {
                error = module_error::stale_id;
                return nullptr;
            }
            error = module_error::ok;
            return &slot;
        }

        registry_slot* reserve_slot_locked() {
            for (registry_slot& slot : slots) {
                if (slot.state == module_state::free) {
                    slot.close_wait  = nullptr;
                    slot.close_owner = false;
                    return &slot;
                }
            }
            return nullptr;
        }

        void abandon_slot(usize index, module_id id) {
            arch::interrupt_spinlock_guard guard(registry_lock);
            registry_slot&                 slot = slots[index];
            if (slot.module.id == id && slot.state == module_state::loading) {
                slot.state       = module_state::free;
                slot.close_wait  = nullptr;
                slot.close_owner = false;
                ++slot.generation;
                slot.module = loaded_module{};
            }
        }

        bool id_from_pointer_locked(const loaded_module* module, module_id& id) {
            for (registry_slot& slot : slots) {
                if (&slot.module == module && slot.state != module_state::free) {
                    id = slot.module.id;
                    return true;
                }
            }
            return false;
        }

        module_error pin_dependencies_locked(module_id             self,
                                             span<const module_id> dependencies) {
            if (dependencies.size() > max_module_dependencies) {
                return module_error::limit;
            }
            for (usize i = 0; i < dependencies.size(); ++i) {
                if (dependencies[i] == self || !dependencies[i].valid()) {
                    return module_error::invalid_argument;
                }
                for (usize j = 0; j < i; ++j) {
                    if (dependencies[i] == dependencies[j]) {
                        return module_error::invalid_argument;
                    }
                }
                module_error   error    = module_error::ok;
                registry_slot* provider = find_slot_locked(dependencies[i], error);
                if (provider == nullptr) {
                    return error;
                }
                if (provider->state != module_state::loaded &&
                    provider->state != module_state::running) {
                    return module_error::stale_id;
                }
                if (provider->module.quiescing) {
                    return module_error::would_block;
                }
            }
            for (usize i = 0; i < dependencies.size(); ++i) {
                module_error   error    = module_error::ok;
                registry_slot* provider = find_slot_locked(dependencies[i], error);
                ++provider->module.dependent_count;
            }
            return module_error::ok;
        }

        /// Transfers the slot's registry reference to the caller, which must
        /// deliver (or otherwise release) the waiter after dropping the lock.
        close_waiter* take_ready_waiter_locked(registry_slot& slot) {
            if (!slot.module.quiescing || slot.module.active_refs != 0 ||
                slot.module.dependent_count != 0 || slot.module.callback_inflight != 0) {
                return nullptr;
            }
            close_waiter* waiter = slot.close_wait;
            slot.close_wait      = nullptr;
            return waiter;
        }

        void collect_wake_locked(registry_slot& slot, unload_plan& plan) {
            close_waiter* waiter = take_ready_waiter_locked(slot);
            if (waiter == nullptr) {
                return;
            }
            if (plan.wake_count < max_modules) {
                plan.wakes[plan.wake_count++] = waiter;
            }
            else {
                slot.close_wait = waiter;
            }
        }

        bool begin_unload_locked(registry_slot& slot, unload_plan& plan) {
            if (slot.close_wait != nullptr) {
                return false;
            }
            plan.index            = static_cast<usize>(&slot - slots);
            plan.id               = slot.module.id;
            plan.base             = slot.module.base;
            plan.size             = slot.module.size;
            plan.context          = slot.module.loader_context;
            plan.unmap            = slot.module.loader_unmap;
            slot.module.quiescing = true;
            slot.state            = module_state::unloading;
            for (usize i = 0; i < slot.module.dependency_count; ++i) {
                module_error   error = module_error::ok;
                registry_slot* provider =
                    find_slot_locked(slot.module.dependencies[i], error);
                if (provider == nullptr) {
                    continue;
                }
                if (provider->module.dependent_count > 0) {
                    --provider->module.dependent_count;
                }
                collect_wake_locked(*provider, plan);
            }
            slot.module.dependency_count = 0;
            return true;
        }

        void perform_unload(unload_plan& plan) {
            if (plan.unmap != nullptr && plan.base != nullptr) {
                plan.unmap(plan.context, plan.base, plan.size);
            }
            {
                arch::interrupt_spinlock_guard guard(registry_lock);
                registry_slot&                 slot = slots[plan.index];
                if (slot.module.id == plan.id && slot.state == module_state::unloading) {
                    slot.state       = module_state::free;
                    slot.close_wait  = nullptr;
                    slot.close_owner = false;
                    ++slot.generation;
                    __builtin_memset(static_cast<void*>(&slot.module), 0,
                                     sizeof slot.module);
                }
            }
            for (usize i = 0; i < plan.wake_count; ++i) {
                deliver_waiter(plan.wakes[i], module_error::ok);
            }
        }

        module_error closable_error(const registry_slot& slot) {
            if (slot.module.resident) {
                return module_error::resident;
            }
            if (slot.state == module_state::running ||
                slot.state == module_state::exiting) {
                return module_error::running;
            }
            if (slot.state != module_state::loaded) {
                return module_error::busy;
            }
            return module_error::ok;
        }

        module_error strict_unloadable_error(const registry_slot& slot) {
            if (slot.module.resident) {
                return module_error::resident;
            }
            if (slot.state == module_state::running ||
                slot.state == module_state::exiting) {
                return module_error::running;
            }
            if (slot.state != module_state::loaded) {
                return module_error::busy;
            }
            if (slot.module.active_refs != 0 || slot.module.dependent_count != 0 ||
                slot.module.callback_inflight != 0) {
                return module_error::busy;
            }
            if (slot.close_owner || slot.close_wait != nullptr) {
                return module_error::busy;
            }
            return module_error::ok;
        }

        bool depends_on_locked(usize start, module_id target, u32& visited) {
            if (start >= max_modules || (visited & (1u << start)) != 0) {
                return false;
            }
            visited |= 1u << start;
            const registry_slot& slot = slots[start];
            for (usize i = 0; i < slot.module.dependency_count; ++i) {
                const module_id dependency = slot.module.dependencies[i];
                if (dependency == target) {
                    return true;
                }
                if (!dependency.valid() || dependency.slot >= max_modules) {
                    continue;
                }
                const registry_slot& provider = slots[dependency.slot];
                if (provider.generation == dependency.generation &&
                    provider.state != module_state::free &&
                    depends_on_locked(dependency.slot, target, visited)) {
                    return true;
                }
            }
            return false;
        }

        /// Frame-allocator-backed memory for the ELF loader.
        ///
        /// Modules are ET_REL, so the loader always maps relocatably (fixed is
        /// never set). Frames are handed out 4 KiB-aligned, which satisfies any
        /// section alignment a compiler actually emits (<= 4096). PVH boot
        /// leaves the kernel on an identity map of physical memory, so the
        /// returned physical address is directly usable — readable, writable
        /// (for relocation), and executable.
        void* image_map(void* context, u64 /*hint*/, usize size, usize align,
                        bool fixed) {
            using fa = mm::frame_allocator;
            if (fixed || size == 0 || align > fa::frame_size) {
                return nullptr; // the frame allocator cannot honor a fixed address
            }
            usize frames = (size + fa::frame_size - 1) / fa::frame_size;
            u64   phys   = mm::kernel_frames().alloc(frames, mm::frame_flags::contiguous);
            if (phys == fa::invalid_frame) {
                return nullptr;
            }
            void* image = reinterpret_cast<void*>(static_cast<usize>(phys));
            if (context != nullptr)
                static_cast<load_state*>(context)->dso = image;
            return image;
        }

        void image_unmap(void*, void* address, usize size) {
            using fa     = mm::frame_allocator;
            usize frames = (size + fa::frame_size - 1) / fa::frame_size;
            mm::kernel_frames().release(reinterpret_cast<u64>(address), frames);
        }

        /// Backs the loader's undefined-symbol resolution with the linked
        /// kernel symbol table. Signature must match elf::loader::resolve_fn.
        u64 image_resolve(void* context, const char* name) {
            if (name != nullptr && name_equals(name, "__dso_handle") &&
                context != nullptr)
                return reinterpret_cast<u64>(static_cast<load_state*>(context)->dso);
            return resolve(name);
        }

        elf::loader::env production_environment(load_state& state) {
            elf::loader::env env{};
            env.ctx     = &state;
            env.map     = image_map;
            env.unmap   = image_unmap;
            env.resolve = image_resolve;
            return env;
        }

        module_error load_image_impl(span<const u8> image, const elf::loader::env& env,
                                     void*                      persistent_context,
                                     const module_load_options& options, module_id& out) {
            out = {};
            if (image.data() == nullptr || image.size() == 0) {
                return module_error::invalid_argument;
            }
            if (options.dependencies.size() > max_module_dependencies) {
                return module_error::limit;
            }

            usize     index = max_modules;
            module_id id{};
            {
                arch::interrupt_spinlock_guard guard(registry_lock);
                registry_slot*                 slot = reserve_slot_locked();
                if (slot == nullptr) {
                    return module_error::no_space;
                }
                index           = static_cast<usize>(slot - slots);
                slot->state     = module_state::loading;
                slot->module    = loaded_module{};
                slot->module.id = module_id{static_cast<u32>(index), slot->generation};
                id              = slot->module.id;
            }

            elf::loader::image loaded{};
            if (!elf::loader::load(image, env, loaded)) {
                abandon_slot(index, id);
                return module_error::load_failed;
            }

            module_error dependency_error = module_error::ok;
            bool         success          = false;
            {
                arch::interrupt_spinlock_guard guard(registry_lock);
                registry_slot&                 slot = slots[index];
                if (slot.state == module_state::loading && slot.module.id == id) {
                    dependency_error = pin_dependencies_locked(id, options.dependencies);
                    if (dependency_error == module_error::ok) {
                        if (options.name[0] != '\0') {
                            copy_module_name(slot.module.name, options.name);
                        }
                        else {
                            const char fallback[max_module_name] = "module";
                            copy_module_name(slot.module.name, fallback);
                        }
                        slot.module.base    = loaded.base;
                        slot.module.size    = loaded.size;
                        slot.module.entry   = reinterpret_cast<int (*)()>(loaded.entry);
                        slot.module.running = false;
                        slot.module.initialized = false;
                        slot.module.init_count  = loaded.init_count;
                        slot.module.fini_count  = loaded.fini_count;
                        for (usize i = 0; i < loaded.init_count; ++i) {
                            slot.module.init_array[i] = loaded.init_array[i];
                        }
                        for (usize i = 0; i < loaded.fini_count; ++i) {
                            slot.module.fini_array[i] = loaded.fini_array[i];
                        }
                        slot.module.loader_context    = persistent_context;
                        slot.module.loader_unmap      = env.unmap;
                        slot.module.dso_identity      = loaded.base;
                        slot.module.resident          = options.resident;
                        slot.module.active_refs       = 0;
                        slot.module.dependent_count   = 0;
                        slot.module.quiescing         = false;
                        slot.module.callback_inflight = 0;
                        slot.module.dependency_count  = options.dependencies.size();
                        for (usize i = 0; i < options.dependencies.size(); ++i) {
                            slot.module.dependencies[i] = options.dependencies[i];
                        }
                        slot.state = module_state::loaded;
                        out        = id;
                        success    = true;
                    }
                }
            }

            if (!success) {
                if (env.unmap != nullptr && loaded.base != nullptr) {
                    env.unmap(env.ctx, loaded.base, loaded.size);
                }
                abandon_slot(index, id);
                return dependency_error == module_error::ok
                           ? module_error::invalid_argument
                           : dependency_error;
            }
            return module_error::ok;
        }

        const loaded_module* pointer_for_id(module_id id) {
            arch::interrupt_spinlock_guard guard(registry_lock);
            module_error                   error = module_error::ok;
            registry_slot*                 slot  = find_slot_locked(id, error);
            return slot == nullptr ? nullptr : &slot->module;
        }

        /// Owns the close invocation for its whole lifetime. The slot cannot be
        /// unloaded by another caller while `close_owner` is set, so a waiter
        /// delivery and the close coroutine's final decision never race slot
        /// reuse. Destroying a parked task clears the registration and releases
        /// both waiter references under the registry lock.
        class close_owner_guard {
            public:
            explicit close_owner_guard(module_id id) : _id(id) {}

            ~close_owner_guard() {
                release();
            }

            close_owner_guard(const close_owner_guard&)            = delete;
            close_owner_guard& operator=(const close_owner_guard&) = delete;

            bool owner() const {
                return _owner;
            }

            void claim() {
                _owner = true;
            }

            void adopt(close_waiter* waiter) {
                _waiter = waiter;
            }

            /// The completion delivered; drop the coroutine reference.
            void finish_wait() {
                if (_waiter != nullptr) {
                    release_waiter(_waiter);
                    _waiter = nullptr;
                }
            }

            private:
            void release() {
                close_waiter* registered = nullptr;
                {
                    arch::interrupt_spinlock_guard guard(registry_lock);
                    module_error                   error = module_error::ok;
                    registry_slot*                 slot  = find_slot_locked(_id, error);
                    if (slot != nullptr) {
                        if (_waiter != nullptr && slot->close_wait == _waiter) {
                            slot->close_wait = nullptr;
                            registered       = _waiter;
                        }
                        if (_owner) {
                            slot->close_owner = false;
                            if (slot->state == module_state::loaded) {
                                slot->module.quiescing = false;
                            }
                        }
                    }
                }
                if (registered != nullptr) {
                    release_waiter(registered);
                }
                if (_waiter != nullptr) {
                    release_waiter(_waiter);
                    _waiter = nullptr;
                }
                _owner = false;
            }

            module_id     _id;
            close_waiter* _waiter = nullptr;
            bool          _owner  = false;
        };

        void fill_info(const registry_slot& slot, module_info& info) {
            info.id = slot.module.id;
            copy_module_name(info.name, slot.module.name);
            info.state            = slot.state;
            info.image_size       = slot.module.size;
            info.active_refs      = slot.module.active_refs;
            info.dependency_count = slot.module.dependency_count;
            info.dependent_count  = slot.module.dependent_count;
            info.running          = slot.module.running;
            info.initialized      = slot.module.initialized;
            info.resident         = slot.module.resident;
            info.quiescing        = slot.module.quiescing;
            info.unloadable       = slot.state == module_state::loaded &&
                              !slot.module.resident && slot.module.active_refs == 0 &&
                              slot.module.dependent_count == 0 &&
                              slot.module.callback_inflight == 0 && !slot.close_owner &&
                              slot.close_wait == nullptr && !slot.module.quiescing;
        }

        /// Snapshot of caller options at call time. `core::task` starts
        /// suspended, so the public open wrapper copies the dependency list
        /// before returning the task; the coroutine frame then owns only value
        /// parameters.
        struct open_parameters {
            bool      resident                              = false;
            bool      too_many                              = false;
            usize     dependency_count                      = 0;
            module_id dependencies[max_module_dependencies] = {};
            char      name[max_module_name]                 = {};
        };

        open_parameters capture_options(const module_load_options& options) {
            open_parameters captured{};
            captured.resident = options.resident;
            copy_module_name(captured.name, options.name);
            if (options.dependencies.size() > max_module_dependencies) {
                captured.too_many = true;
                return captured;
            }
            captured.dependency_count = options.dependencies.size();
            for (usize i = 0; i < options.dependencies.size(); ++i) {
                captured.dependencies[i] = options.dependencies[i];
            }
            return captured;
        }

        module_load_options options_from(const open_parameters& captured) {
            module_load_options options{};
            options.resident = captured.resident;
            options.dependencies =
                span<const module_id>(captured.dependencies, captured.dependency_count);
            copy_module_name(options.name, captured.name);
            return options;
        }

        core::task<module_open_result> open_task(span<const u8>   image,
                                                 elf::loader::env env,
                                                 open_parameters  parameters) {
            module_open_result result{};
            if (parameters.too_many) {
                result.error = module_error::limit;
                co_return result;
            }
            const module_load_options options = options_from(parameters);
            result.error = load_image(image, env, options, result.id);
            co_return result;
        }

        core::task<module_open_result> open_task(span<const u8>  image,
                                                 open_parameters parameters) {
            module_open_result result{};
            if (parameters.too_many) {
                result.error = module_error::limit;
                co_return result;
            }
            load_state                state{};
            const elf::loader::env    env     = production_environment(state);
            const module_load_options options = options_from(parameters);
            result.error = load_image(image, env, options, result.id);
            co_return result;
        }

    } // namespace

    int module_errno(module_error error) noexcept {
        switch (error) {
        case module_error::ok:
            return 0;
        case module_error::invalid_argument:
        case module_error::not_running:
        case module_error::dependency:
            return -EINVAL;
        case module_error::not_found:
        case module_error::stale_id:
            return -ENOENT;
        case module_error::no_space:
            return -ENOSPC;
        case module_error::no_memory:
            return -ENOMEM;
        case module_error::limit:
            return -E2BIG;
        case module_error::load_failed:
            return -ENOEXEC;
        case module_error::busy:
        case module_error::running:
            return -EBUSY;
        case module_error::would_block:
            return -EAGAIN;
        case module_error::resident:
            return -EPERM;
        case module_error::cycle:
            return -EDEADLK;
        case module_error::access:
            return -EACCES;
        case module_error::canceled:
            return -ECANCELED;
        }
        return -EINVAL;
    }

    core::task<module_open_result> open(span<const u8> image, const elf::loader::env& env,
                                        const module_load_options& options) {
        return open_task(image, env, capture_options(options));
    }

    core::task<module_open_result> open(span<const u8>             image,
                                        const module_load_options& options) {
        return open_task(image, capture_options(options));
    }

    module_error load_image(span<const u8> image, const elf::loader::env& env,
                            const module_load_options& options, module_id& out) {
        return load_image_impl(image, env, env.ctx, options, out);
    }

    module_error load_image(span<const u8> image, const module_load_options& options,
                            module_id& out) {
        load_state       state{};
        elf::loader::env env = production_environment(state);
        return load_image_impl(image, env, nullptr, options, out);
    }

    module_error run(module_id id, int& result) {
        result = -1;
        execution_guard execution;
        if (!execution.owned()) {
            return module_error::busy;
        }
        loaded_module* target = nullptr;
        {
            arch::interrupt_spinlock_guard guard(registry_lock);
            module_error                   error = module_error::ok;
            registry_slot*                 slot  = find_slot_locked(id, error);
            if (slot == nullptr) {
                return error;
            }
            if (slot->state == module_state::running) {
                return module_error::busy;
            }
            if (slot->state != module_state::loaded) {
                return slot->state == module_state::loading
                           ? module_error::busy
                           : module_error::invalid_argument;
            }
            if (slot->module.quiescing) {
                return module_error::would_block;
            }
            slot->state               = module_state::running;
            slot->module.running      = true;
            slot->module.entry_active = true;
            target                    = &slot->module;
        }

        loaded_module* previous       = nullptr;
        void*          previous_owner = nullptr;
        {
            arch::interrupt_spinlock_guard guard(callback_lock);
            previous       = active;
            previous_owner = callback_owner;
            active         = target;
            callback_owner = callback_context_owner();
        }
        for (usize i = 0; i < target->init_count; ++i) {
            if (target->init_array[i] != nullptr) {
#ifdef ASTIA_MODULE_HOST_TEST
                host_test::invoke_initializer(target->init_array[i]);
#else
                target->init_array[i]();
#endif
            }
        }
        {
            arch::interrupt_spinlock_guard guard(registry_lock);
            target->initialized = true;
        }
#ifdef ASTIA_MODULE_HOST_TEST
        result = host_test::invoke_entry(target->entry);
#else
        result = target->entry();
#endif
        {
            arch::interrupt_spinlock_guard guard(callback_lock);
            active         = previous;
            callback_owner = previous_owner;
        }
        {
            arch::interrupt_spinlock_guard guard(registry_lock);
            target->entry_active = false;
        }
        return module_error::ok;
    }

    module_error exit(module_id id) {
        execution_guard execution;
        if (!execution.owned()) {
            return module_error::busy;
        }
        loaded_module* target = nullptr;
        {
            arch::interrupt_spinlock_guard guard(registry_lock);
            module_error                   error = module_error::ok;
            registry_slot*                 slot  = find_slot_locked(id, error);
            if (slot == nullptr) {
                return error;
            }
            if (slot->state != module_state::running) {
                return module_error::not_running;
            }
            if (slot->module.entry_active) {
                return module_error::busy;
            }
            slot->state = module_state::exiting;
            target      = &slot->module;
        }

        loaded_module* previous       = nullptr;
        void*          previous_owner = nullptr;
        {
            arch::interrupt_spinlock_guard guard(callback_lock);
            previous       = active;
            previous_owner = callback_owner;
            active         = target;
            callback_owner = callback_context_owner();
        }
        finalize_exit_handlers(nullptr);
        for (usize i = target->fini_count; i != 0; --i) {
            if (target->fini_array[i - 1] != nullptr) {
#ifdef ASTIA_MODULE_HOST_TEST
                host_test::invoke_finalizer(target->fini_array[i - 1]);
#else
                target->fini_array[i - 1]();
#endif
            }
        }
        finalize_exit_handlers(nullptr);
        {
            arch::interrupt_spinlock_guard guard(callback_lock);
            active         = previous;
            callback_owner = previous_owner;
        }

        arch::interrupt_spinlock_guard guard(registry_lock);
        target->initialized  = false;
        target->running      = false;
        target->entry_active = false;
        for (registry_slot& slot : slots) {
            if (&slot.module == target && slot.state == module_state::exiting) {
                slot.state = module_state::loaded;
                break;
            }
        }
        return module_error::ok;
    }

    module_error unload(module_id id) {
        unload_plan plan{};
        {
            arch::interrupt_spinlock_guard guard(registry_lock);
            module_error                   error = module_error::ok;
            registry_slot*                 slot  = find_slot_locked(id, error);
            if (slot == nullptr) {
                return error;
            }
            error = strict_unloadable_error(*slot);
            if (error != module_error::ok) {
                return error;
            }
            if (!begin_unload_locked(*slot, plan)) {
                return module_error::busy;
            }
        }
        perform_unload(plan);
        return module_error::ok;
    }

    core::task<module_error> close(module_id id) {
        close_owner_guard ownership(id);
        for (;;) {
            close_waiter* waiter = new (std::nothrow) close_waiter();
            if (waiter == nullptr) {
                co_return module_error::no_memory;
            }
            ownership.adopt(waiter);
            unload_plan  plan{};
            module_error early = module_error::ok;
            bool         wait  = false;
            {
                arch::interrupt_spinlock_guard guard(registry_lock);
                module_error                   error = module_error::ok;
                registry_slot*                 slot  = find_slot_locked(id, error);
                if (slot == nullptr) {
                    early = error;
                }
                else if (slot->close_owner && !ownership.owner()) {
                    early = module_error::busy;
                }
                else {
                    early = closable_error(*slot);
                }
                if (early == module_error::ok) {
                    if (!ownership.owner()) {
                        slot->close_owner = true;
                        ownership.claim();
                    }
                    slot->module.quiescing = true;
                    if (slot->module.active_refs == 0 &&
                        slot->module.dependent_count == 0 &&
                        slot->module.callback_inflight == 0) {
                        if (!begin_unload_locked(*slot, plan)) {
                            early = module_error::busy;
                        }
                    }
                    else if (slot->close_wait != nullptr) {
                        early = module_error::busy;
                    }
                    else {
                        retain_waiter(waiter);
                        slot->close_wait = waiter;
                        wait             = true;
                    }
                }
            }

            if (early != module_error::ok) {
                co_return early;
            }
            if (!wait) {
                perform_unload(plan);
                co_return module_error::ok;
            }

            auto result = co_await waiter->completion;
            ownership.finish_wait();
            if (!result) {
                co_return module_error::canceled;
            }
            if (result.value != module_error::ok) {
                co_return result.value;
            }
        }
    }

    module_error cancel_close(module_id id) {
        close_waiter* waiter = nullptr;
        {
            arch::interrupt_spinlock_guard guard(registry_lock);
            module_error                   error = module_error::ok;
            registry_slot*                 slot  = find_slot_locked(id, error);
            if (slot == nullptr) {
                return error;
            }
            if (!slot->close_owner) {
                return module_error::ok;
            }
            if (slot->close_wait == nullptr) {
                return module_error::busy;
            }
            waiter                 = slot->close_wait;
            slot->close_wait       = nullptr;
            slot->module.quiescing = false;
        }
        deliver_waiter(waiter, module_error::canceled);
        return module_error::ok;
    }

    module_error lookup(module_id id, module_info& out) {
        arch::interrupt_spinlock_guard guard(registry_lock);
        module_error                   error = module_error::ok;
        registry_slot*                 slot  = find_slot_locked(id, error);
        if (slot == nullptr) {
            return error;
        }
        if (!published(slot->state)) {
            return module_error::busy;
        }
        fill_info(*slot, out);
        return module_error::ok;
    }

    usize snapshot(span<module_info> out, usize* total) {
        arch::interrupt_spinlock_guard guard(registry_lock);
        usize                          written = 0;
        usize                          visible = 0;
        for (const registry_slot& slot : slots) {
            if (!published(slot.state)) {
                continue;
            }
            ++visible;
            if (written < out.size()) {
                fill_info(slot, out[written++]);
            }
        }
        if (total != nullptr) {
            *total = visible;
        }
        return written;
    }

    module_error snapshot_module(module_id id, module_info& out,
                                 span<module_id> dependencies, usize& dependency_total) {
        dependency_total = 0;
        arch::interrupt_spinlock_guard guard(registry_lock);
        module_error                   error = module_error::ok;
        registry_slot*                 slot  = find_slot_locked(id, error);
        if (slot == nullptr) {
            return error;
        }
        if (!published(slot->state)) {
            return module_error::busy;
        }
        fill_info(*slot, out);
        dependency_total  = slot->module.dependency_count;
        const usize count = dependencies.size() < slot->module.dependency_count
                                ? dependencies.size()
                                : slot->module.dependency_count;
        for (usize i = 0; i < count; ++i) {
            dependencies[i] = slot->module.dependencies[i];
        }
        return module_error::ok;
    }

    module_error dependency_at(module_id id, usize index, module_id& out) {
        out = {};
        arch::interrupt_spinlock_guard guard(registry_lock);
        module_error                   error = module_error::ok;
        registry_slot*                 slot  = find_slot_locked(id, error);
        if (slot == nullptr) {
            return error;
        }
        if (!published(slot->state)) {
            return module_error::busy;
        }
        if (index >= slot->module.dependency_count) {
            return module_error::not_found;
        }
        out = slot->module.dependencies[index];
        return module_error::ok;
    }

    module_error add_dependency(module_id dependent, module_id dependency) {
        arch::interrupt_spinlock_guard guard(registry_lock);
        module_error                   error = module_error::ok;
        registry_slot* dependent_slot        = find_slot_locked(dependent, error);
        if (dependent_slot == nullptr) {
            return error;
        }
        registry_slot* provider_slot = find_slot_locked(dependency, error);
        if (provider_slot == nullptr) {
            return error;
        }
        if (dependent == dependency) {
            return module_error::invalid_argument;
        }
        if ((dependent_slot->state != module_state::loaded &&
             dependent_slot->state != module_state::running) ||
            (provider_slot->state != module_state::loaded &&
             provider_slot->state != module_state::running)) {
            return module_error::busy;
        }
        if (dependent_slot->module.quiescing || provider_slot->module.quiescing) {
            return module_error::would_block;
        }
        for (usize i = 0; i < dependent_slot->module.dependency_count; ++i) {
            if (dependent_slot->module.dependencies[i] == dependency) {
                return module_error::ok;
            }
        }
        if (dependent_slot->module.dependency_count == max_module_dependencies) {
            return module_error::limit;
        }
        u32 visited = 0;
        if (depends_on_locked(dependency.slot, dependent, visited)) {
            return module_error::cycle;
        }
        dependent_slot->module.dependencies[dependent_slot->module.dependency_count++] =
            dependency;
        ++provider_slot->module.dependent_count;
        return module_error::ok;
    }

    module_error remove_dependency(module_id dependent, module_id dependency) {
        close_waiter* waiter = nullptr;
        {
            arch::interrupt_spinlock_guard guard(registry_lock);
            module_error                   error = module_error::ok;
            registry_slot* dependent_slot        = find_slot_locked(dependent, error);
            if (dependent_slot == nullptr) {
                return error;
            }
            bool found = false;
            for (usize i = 0; i < dependent_slot->module.dependency_count; ++i) {
                if (dependent_slot->module.dependencies[i] != dependency) {
                    continue;
                }
                dependent_slot->module.dependencies[i] =
                    dependent_slot->module
                        .dependencies[--dependent_slot->module.dependency_count];
                found = true;
                break;
            }
            if (!found) {
                return module_error::not_found;
            }
            registry_slot* provider_slot = find_slot_locked(dependency, error);
            if (provider_slot != nullptr && provider_slot->module.dependent_count > 0) {
                --provider_slot->module.dependent_count;
                waiter = take_ready_waiter_locked(*provider_slot);
            }
        }
        if (waiter != nullptr) {
            deliver_waiter(waiter, module_error::ok);
        }
        return module_error::ok;
    }

    module_error acquire(module_id id) {
        arch::interrupt_spinlock_guard guard(registry_lock);
        module_error                   error = module_error::ok;
        registry_slot*                 slot  = find_slot_locked(id, error);
        if (slot == nullptr) {
            return error;
        }
        if (slot->module.quiescing) {
            return module_error::would_block;
        }
        if (slot->state != module_state::loaded && slot->state != module_state::running) {
            return module_error::busy;
        }
        if (slot->module.active_refs == ~u32{0}) {
            return module_error::limit;
        }
        ++slot->module.active_refs;
        return module_error::ok;
    }

    module_error release(module_id id) {
        close_waiter* waiter = nullptr;
        {
            arch::interrupt_spinlock_guard guard(registry_lock);
            module_error                   error = module_error::ok;
            registry_slot*                 slot  = find_slot_locked(id, error);
            if (slot == nullptr) {
                return error;
            }
            if (slot->module.active_refs == 0) {
                return module_error::invalid_argument;
            }
            --slot->module.active_refs;
            waiter = take_ready_waiter_locked(*slot);
        }
        if (waiter != nullptr) {
            deliver_waiter(waiter, module_error::ok);
        }
        return module_error::ok;
    }

    bool quiesced(module_id id) {
        arch::interrupt_spinlock_guard guard(registry_lock);
        module_error                   error = module_error::ok;
        registry_slot*                 slot  = find_slot_locked(id, error);
        return slot != nullptr && slot->state == module_state::loaded &&
               !slot->module.running && slot->module.active_refs == 0;
    }

    module_error make_resident(module_id id) {
        arch::interrupt_spinlock_guard guard(registry_lock);
        module_error                   error = module_error::ok;
        registry_slot*                 slot  = find_slot_locked(id, error);
        if (slot == nullptr) {
            return error;
        }
        if (!published(slot->state)) {
            return module_error::busy;
        }
        if (slot->module.quiescing) {
            return module_error::would_block;
        }
        slot->module.resident = true;
        return module_error::ok;
    }

#ifdef ASTIA_MODULE_HOST_TEST
    module_error clear_resident_for_test(module_id id) {
        arch::interrupt_spinlock_guard guard(registry_lock);
        module_error                   error = module_error::ok;
        registry_slot*                 slot  = find_slot_locked(id, error);
        if (slot == nullptr) {
            return error;
        }
        slot->module.resident = false;
        return module_error::ok;
    }
#endif

    const loaded_module* load_module(span<const u8> image) {
        module_id id{};
        if (load_image(image, module_load_options{}, id) != module_error::ok) {
            return nullptr;
        }
        return pointer_for_id(id);
    }

    const loaded_module* load_module(span<const u8> image, const elf::loader::env& env) {
        module_id id{};
        if (load_image(image, env, module_load_options{}, id) != module_error::ok) {
            return nullptr;
        }
        return pointer_for_id(id);
    }

    bool unload_module(const loaded_module* mod) {
        if (mod == nullptr) {
            return false;
        }
        module_id id{};
        {
            arch::interrupt_spinlock_guard guard(registry_lock);
            if (!id_from_pointer_locked(mod, id)) {
                return false;
            }
        }
        return unload(id) == module_error::ok;
    }

    int run_module(const loaded_module* mod) {
        if (mod == nullptr) {
            return -1;
        }
        module_id id{};
        {
            arch::interrupt_spinlock_guard guard(registry_lock);
            if (!id_from_pointer_locked(mod, id)) {
                return -1;
            }
        }
        int result = -1;
        if (run(id, result) != module_error::ok) {
            return -1;
        }
        return result;
    }

    bool exit_module(const loaded_module* mod) {
        if (mod == nullptr) {
            return false;
        }
        module_id id{};
        {
            arch::interrupt_spinlock_guard guard(registry_lock);
            if (!id_from_pointer_locked(mod, id)) {
                return false;
            }
        }
        return exit(id) == module_error::ok;
    }

    int register_exit_handler(void (*handler)(void)) {
        if (handler == nullptr)
            return -1;
        arch::interrupt_spinlock_guard guard(callback_lock);
        if (!owns_callback_context_locked())
            return -1;
        for (usize i = 0; i < active->exit_count; ++i) {
            if (!active->exit_handlers[i].removed)
                continue;
            active->exit_handlers[i] = {handler, nullptr, nullptr, active->dso_identity,
                                        false};
            return 0;
        }
        if (active->exit_count == loaded_module::max_exit_handlers)
            return -1;
        active->exit_handlers[active->exit_count++] = {handler, nullptr, nullptr,
                                                       active->dso_identity, false};
        return 0;
    }

    int register_exit_handler(void (*handler)(void*), void* argument, void* dso) {
        if (handler == nullptr)
            return -1;
        arch::interrupt_spinlock_guard guard(callback_lock);
        if (!owns_callback_context_locked())
            return -1;
        void* identity = dso != nullptr ? dso : active->dso_identity;
        for (usize i = 0; i < active->exit_count; ++i) {
            if (!active->exit_handlers[i].removed)
                continue;
            active->exit_handlers[i] = {nullptr, handler, argument, identity, false};
            return 0;
        }
        if (active->exit_count == loaded_module::max_exit_handlers)
            return -1;
        active->exit_handlers[active->exit_count++] = {nullptr, handler, argument,
                                                       identity, false};
        return 0;
    }

    void finalize_exit_handlers(void* dso) {
        for (;;) {
            loaded_module::exit_handler handler{};
            loaded_module*              target = nullptr;
            {
                arch::interrupt_spinlock_guard guard(callback_lock);
                if (!owns_callback_context_locked())
                    return;
                usize selected = active->exit_count;
                for (usize i = active->exit_count; i != 0; --i) {
                    const loaded_module::exit_handler& candidate =
                        active->exit_handlers[i - 1];
                    if (!candidate.removed && (dso == nullptr || candidate.dso == dso)) {
                        selected = i - 1;
                        break;
                    }
                }
                if (selected == active->exit_count)
                    break;
                active->exit_handlers[selected].removed = true;
                handler                                 = active->exit_handlers[selected];
                target                                  = active;
                arch::interrupt_spinlock_guard registry_guard(registry_lock);
                ++target->callback_inflight;
            }

            if (handler.plain != nullptr)
                handler.plain();
            else if (handler.cxa != nullptr)
                handler.cxa(handler.argument);

            close_waiter* waiter = nullptr;
            {
                arch::interrupt_spinlock_guard guard(registry_lock);
                if (target->callback_inflight > 0)
                    --target->callback_inflight;
                if (target->callback_inflight == 0) {
                    module_error   error = module_error::ok;
                    registry_slot* slot  = find_slot_locked(target->id, error);
                    if (slot != nullptr)
                        waiter = take_ready_waiter_locked(*slot);
                }
            }
            deliver_waiter(waiter, module_error::ok);
        }

        arch::interrupt_spinlock_guard guard(callback_lock);
        if (!owns_callback_context_locked())
            return;
        usize remaining = 0;
        for (usize i = 0; i < active->exit_count; ++i)
            if (!active->exit_handlers[i].removed)
                active->exit_handlers[remaining++] = active->exit_handlers[i];
        active->exit_count = remaining;
    }

    usize count() {
        arch::interrupt_spinlock_guard guard(registry_lock);
        usize                          count = 0;
        for (const registry_slot& slot : slots) {
            if (published(slot.state)) {
                ++count;
            }
        }
        return count;
    }

    const loaded_module* at(usize i) {
        arch::interrupt_spinlock_guard guard(registry_lock);
        usize                          seen = 0;
        for (registry_slot& slot : slots) {
            if (!published(slot.state)) {
                continue;
            }
            if (seen == i) {
                return &slot.module;
            }
            ++seen;
        }
        return nullptr;
    }

} // namespace astia::mod

extern "C" int astia_module_register_atexit(void (*handler)(void)) {
    return astia::mod::register_exit_handler(handler);
}

extern "C" int astia_module_register_cxa_atexit(void (*handler)(void*), void* argument,
                                                void* dso) {
    return astia::mod::register_exit_handler(handler, argument, dso);
}

extern "C" void astia_module_finalize_cxa(void* dso) {
    return astia::mod::finalize_exit_handlers(dso);
}
