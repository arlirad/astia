// Module registry: the policy layer over the pure ELF loader.
//
// Memory comes from the kernel frame allocator; undefined symbols resolve
// against the linked kernel symbol table. Both are wired in here as plain
// callbacks — no dependency injection, the loader just takes function pointers.

#include "module.hpp"

#include <astia/elf.hpp>
#include <astia/mm/frame_allocator.hpp>

#include "exports.hpp"

namespace astia::mod {

#ifdef ASTIA_MODULE_HOST_TEST
    namespace host_test {
        int  invoke_entry(int (*entry)());
        void invoke_initializer(elf::loader::init_fn initializer);
        void invoke_finalizer(elf::loader::init_fn finalizer);
    }
#endif

    namespace {

        loaded_module  modules[max_modules] = {};
        usize          used                 = 0;
        loaded_module* active               = nullptr;
        struct load_state {
            void* dso = nullptr;
        } production_load_state;

        bool name_equals(const char* left, const char* right) {
            while (*left != '\0' && *left == *right) {
                ++left;
                ++right;
            }
            return *left == *right;
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

    } // namespace

    const loaded_module* load_module(span<const u8> image) {
        production_load_state = {};
        elf::loader::env e{};
        e.ctx     = &production_load_state;
        e.map     = image_map;
        e.unmap   = image_unmap;
        e.resolve = image_resolve;
        return load_module(image, e);
    }

    const loaded_module* load_module(span<const u8> image, const elf::loader::env& e) {
        if (image.data() == nullptr || image.size() == 0 || used >= max_modules) {
            return nullptr;
        }

        elf::loader::image img;
        if (!elf::loader::load(image, e, img)) {
            return nullptr;
        }

        loaded_module& slot = modules[used];
        slot.name           = "module";
        slot.base           = img.base;
        slot.size           = img.size;
        slot.entry          = reinterpret_cast<int (*)()>(img.entry);
        slot.running        = false;
        slot.initialized    = false;
        slot.init_count     = img.init_count;
        slot.fini_count     = img.fini_count;
        slot.loader_context = e.ctx;
        slot.loader_unmap   = e.unmap;
        slot.exit_count     = 0;
        slot.dso_identity   = img.base;
        for (usize i = 0; i < img.init_count; ++i)
            slot.init_array[i] = img.init_array[i];
        for (usize i = 0; i < img.fini_count; ++i)
            slot.fini_array[i] = img.fini_array[i];
        ++used;
        return &slot;
    }

    bool unload_module(const loaded_module* mod) {
        if (mod == nullptr) {
            return false;
        }
        for (usize i = 0; i < used; ++i) {
            if (&modules[i] != mod) {
                continue;
            }
            if (mod->running) {
                return false;
            }
            // The loader makes one contiguous allocation, so it is one
            // contiguous release.
            if (mod->loader_unmap == nullptr) {
                return false;
            }
            mod->loader_unmap(mod->loader_context, mod->base, mod->size);
            // Compact the registry.
            for (usize j = i; j + 1 < used; ++j) {
                modules[j] = modules[j + 1];
            }
            --used;
            return true;
        }
        return false;
    }

    int run_module(const loaded_module* mod) {
        if (mod == nullptr || mod->running || mod->entry == nullptr) {
            return -1;
        }
        const_cast<loaded_module*>(mod)->running = true;
        loaded_module* mutable_mod               = const_cast<loaded_module*>(mod);
        loaded_module* previous                  = active;
        active                                   = mutable_mod;
        for (usize i = 0; i < mutable_mod->init_count; ++i)
            if (mutable_mod->init_array[i] != nullptr) {
#ifdef ASTIA_MODULE_HOST_TEST
                host_test::invoke_initializer(mutable_mod->init_array[i]);
#else
                mutable_mod->init_array[i]();
#endif
            }
        mutable_mod->initialized = true;
#ifdef ASTIA_MODULE_HOST_TEST
        const int result = host_test::invoke_entry(mutable_mod->entry);
#else
        const int result = mutable_mod->entry();
#endif
        active = previous;
        return result;
    }

    bool exit_module(const loaded_module* mod) {
        if (mod == nullptr || !mod->running)
            return false;
        loaded_module* mutable_mod = const_cast<loaded_module*>(mod);
        loaded_module* previous    = active;
        active                     = mutable_mod;
        finalize_exit_handlers(nullptr);
        for (usize i = mutable_mod->fini_count; i != 0; --i)
            if (mutable_mod->fini_array[i - 1] != nullptr) {
#ifdef ASTIA_MODULE_HOST_TEST
                host_test::invoke_finalizer(mutable_mod->fini_array[i - 1]);
#else
                mutable_mod->fini_array[i - 1]();
#endif
            }
        finalize_exit_handlers(nullptr);
        active                   = previous;
        mutable_mod->initialized = false;
        mutable_mod->running     = false;
        return true;
    }

    int register_exit_handler(void (*handler)(void)) {
        if (active == nullptr || handler == nullptr)
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
        if (active == nullptr || handler == nullptr)
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
        if (active == nullptr)
            return;
        for (;;) {
            usize selected = active->exit_count;
            for (usize i = active->exit_count; i != 0; --i) {
                const loaded_module::exit_handler& handler = active->exit_handlers[i - 1];
                if (!handler.removed && (dso == nullptr || handler.dso == dso)) {
                    selected = i - 1;
                    break;
                }
            }
            if (selected == active->exit_count)
                break;
            loaded_module::exit_handler& handler = active->exit_handlers[selected];
            handler.removed                      = true;
            if (handler.plain != nullptr)
                handler.plain();
            else if (handler.cxa != nullptr)
                handler.cxa(handler.argument);
        }
        usize remaining = 0;
        for (usize i = 0; i < active->exit_count; ++i)
            if (!active->exit_handlers[i].removed)
                active->exit_handlers[remaining++] = active->exit_handlers[i];
        active->exit_count = remaining;
    }

    usize count() {
        return used;
    }

    const loaded_module* at(usize i) {
        return i < used ? &modules[i] : nullptr;
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
    astia::mod::finalize_exit_handlers(dso);
}
