// Module registry: the policy layer over the pure ELF loader.
//
// Memory comes from the kernel frame allocator; undefined symbols resolve
// against the linked kernel symbol table. Both are wired in here as plain
// callbacks — no dependency injection, the loader just takes function pointers.

#include "module.hpp"

#include "exports.hpp"
#include "module_driver.hpp"
#include <astia/elf.hpp>
#include <astia/mm/frame_allocator.hpp>

namespace astia::mod {

    extern "C" void __cxa_finalize(void*);
    bool            teardown_module(const loaded_module&) __attribute__((weak));

#ifdef ASTIA_MODULE_HOST_TEST
    namespace host_test {
        int  invoke_entry(int (*entry)());
        void invoke_initializer(elf::init_fn initializer);
        void invoke_finalizer(elf::init_fn finalizer);
        void invoke_finalize(void* dso_handle);
    }
#endif

    namespace {

        loaded_module        modules[max_modules] = {};
        usize                used                 = 0;
        u64                  next_dso_handle      = 1;
        const loaded_module* active_module        = nullptr;

        struct load_context {
            const elf::env* source;
            void*           dso_handle;
        };

        void* map_image_for_module(void* context, u64 hint, usize size, usize align,
                                   bool fixed) {
            auto* load = static_cast<load_context*>(context);
            return load->source->map(load->source->ctx, hint, size, align, fixed);
        }

        void unmap_image_for_module(void* context, void* address, usize size) {
            auto* load = static_cast<load_context*>(context);
            load->source->unmap(load->source->ctx, address, size);
        }

        bool symbol_is_dso_handle(const char* name) {
            constexpr char dso_name[] = "__dso_handle";
            usize          i          = 0;
            while (dso_name[i] != '\0' && name[i] == dso_name[i])
                ++i;
            return dso_name[i] == '\0' && name[i] == '\0';
        }

        u64 resolve_image_for_module(void* context, const char* name) {
            auto* load = static_cast<load_context*>(context);
            if (symbol_is_dso_handle(name))
                return reinterpret_cast<u64>(load->dso_handle);
            return load->source->resolve == nullptr
                       ? 0
                       : load->source->resolve(load->source->ctx, name);
        }

        void* allocate_dso_handle() {
            const u64 value = next_dso_handle++;
            return reinterpret_cast<void*>(static_cast<usize>(value == 0 ? 1 : value));
        }

        /// Frame-allocator-backed memory for the ELF loader.
        ///
        /// Modules are ET_REL, so the loader always maps relocatably (fixed is
        /// never set). Frames are handed out 4 KiB-aligned, which satisfies any
        /// section alignment a compiler actually emits (<= 4096). PVH boot
        /// leaves the kernel on an identity map of physical memory, so the
        /// returned physical address is directly usable — readable, writable
        /// (for relocation), and executable.
        void* image_map(void*, u64 /*hint*/, usize size, usize align, bool fixed) {
            using fa = mm::frame_allocator;
            if (fixed || size == 0 || align > fa::frame_size) {
                return nullptr; // the frame allocator cannot honor a fixed address
            }
            usize frames = (size + fa::frame_size - 1) / fa::frame_size;
            u64   phys   = mm::kernel_frames().alloc(frames, mm::frame_flags::contiguous);
            if (phys == fa::invalid_frame) {
                return nullptr;
            }
            return reinterpret_cast<void*>(static_cast<usize>(phys));
        }

        void image_unmap(void*, void* address, usize size) {
            using fa     = mm::frame_allocator;
            usize frames = (size + fa::frame_size - 1) / fa::frame_size;
            mm::kernel_frames().release(reinterpret_cast<u64>(address), frames);
        }

        /// Backs the loader's undefined-symbol resolution with the linked
        /// kernel symbol table. Signature must match elf::resolve_fn.
        u64 image_resolve(void*, const char* name) {
            return resolve(name);
        }

    } // namespace

    const loaded_module* load_module(span<const u8> image) {
        elf::env e{};
        e.ctx     = nullptr;
        e.map     = image_map;
        e.unmap   = image_unmap;
        e.resolve = image_resolve;
        return load_module(image, e);
    }

    const loaded_module* load_module(span<const u8> image, const elf::env& e) {
        if (image.data() == nullptr || image.size() == 0 || used >= max_modules) {
            return nullptr;
        }

        load_context context{&e, allocate_dso_handle()};
        elf::env     module_env{};
        module_env.ctx     = &context;
        module_env.map     = map_image_for_module;
        module_env.unmap   = unmap_image_for_module;
        module_env.resolve = resolve_image_for_module;

        elf::image img;
        if (!elf::load(image, module_env, img)) {
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
        slot.dso_handle     = context.dso_handle;
        slot.loader_context = e.ctx;
        slot.loader_unmap   = e.unmap;
        slot.finalized      = false;
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
        loaded_module*       mutable_mod         = const_cast<loaded_module*>(mod);
        const loaded_module* previous            = active_module;
        active_module                            = mutable_mod;
        for (usize i = 0; i < mutable_mod->init_count; ++i)
            if (mutable_mod->init_array[i] != nullptr) {
#ifdef ASTIA_MODULE_HOST_TEST
                host_test::invoke_initializer(mutable_mod->init_array[i]);
#else
                mutable_mod->init_array[i]();
#endif
                if (!mutable_mod->running) {
                    active_module = previous;
                    return -1;
                }
            }
        mutable_mod->initialized = true;
        const int result =
#ifdef ASTIA_MODULE_HOST_TEST
            host_test::invoke_entry(mutable_mod->entry);
#else
            mutable_mod->entry();
#endif
        active_module = previous;
        return result;
    }

    bool exit_module(const loaded_module* mod) {
        if (mod == nullptr || !mod->running)
            return false;
        loaded_module* mutable_mod = const_cast<loaded_module*>(mod);
        if (teardown_module != nullptr && !teardown_module(*mutable_mod))
            return false;
        if (mutable_mod->dso_handle != nullptr && !mutable_mod->finalized) {
#ifdef ASTIA_MODULE_HOST_TEST
            host_test::invoke_finalize(mutable_mod->dso_handle);
#else
            __cxa_finalize(mutable_mod->dso_handle);
#endif
            mutable_mod->finalized = true;
        }
        for (usize i = mutable_mod->fini_count; i != 0; --i)
            if (mutable_mod->fini_array[i - 1] != nullptr) {
#ifdef ASTIA_MODULE_HOST_TEST
                host_test::invoke_finalizer(mutable_mod->fini_array[i - 1]);
#else
                mutable_mod->fini_array[i - 1]();
#endif
            }
        mutable_mod->initialized = false;
        mutable_mod->running     = false;
        return true;
    }

    const loaded_module* current_module() {
        return active_module;
    }

    usize count() {
        return used;
    }

    const loaded_module* at(usize i) {
        return i < used ? &modules[i] : nullptr;
    }

} // namespace astia::mod
