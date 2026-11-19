// Module registry: the policy layer over the pure ELF loader.
//
// Memory comes from the kernel frame allocator; undefined symbols resolve
// against the linked kernel symbol table. Both are wired in here as plain
// callbacks — no dependency injection, the loader just takes function pointers.

#include "module.hpp"

#include <astia/elf.hpp>
#include <astia/mm/frame_allocator.hpp>

#include "exports.hpp"
#include "module_legacy_driver.hpp"

namespace astia::mod {

#ifdef ASTIA_MODULE_HOST_TEST
    namespace host_test {
        int  invoke_entry(int (*entry)());
        void invoke_initializer(elf::init_fn initializer);
        void invoke_finalizer(elf::init_fn finalizer);
    }
#endif

    namespace {

        loaded_module  modules[max_modules] = {};
        usize          used                 = 0;
        loaded_module* active_module        = nullptr;

        [[maybe_unused]] int invoke_legacy_entry(const loaded_module& mod) {
            return mod.entry();
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

        elf::image img;
        if (!elf::load(image, e, img)) {
            return nullptr;
        }

        loaded_module& slot = modules[used];
        slot.name           = "module";
        slot.base           = img.base;
        slot.size           = img.size;
        slot.entry          = reinterpret_cast<int (*)()>(img.entry);
        slot.legacy_entry   = img.legacy_entry;
        slot.running        = false;
        slot.initialized    = false;
        slot.init_count     = img.init_count;
        slot.fini_count     = img.fini_count;
        slot.loader_context = e.ctx;
        slot.loader_unmap   = e.unmap;
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
        loaded_module* previous                  = active_module;
        active_module                            = const_cast<loaded_module*>(mod);
        const_cast<loaded_module*>(mod)->running = true;
        loaded_module* mutable_mod               = const_cast<loaded_module*>(mod);
        for (usize i = 0; i < mutable_mod->init_count; ++i)
            if (mutable_mod->init_array[i] != nullptr) {
#ifdef ASTIA_MODULE_HOST_TEST
                host_test::invoke_initializer(mutable_mod->init_array[i]);
#else
                mutable_mod->init_array[i]();
#endif
            }
        mutable_mod->initialized = true;
        int result               = 0;
#ifdef ASTIA_MODULE_HOST_TEST
        result = host_test::invoke_entry(mutable_mod->entry);
#else
        result = mutable_mod->legacy_entry ? invoke_legacy_entry(*mutable_mod)
                                           : mutable_mod->entry();
#endif
        active_module = previous;
        return result;
    }

    bool exit_module(const loaded_module* mod) {
        if (mod == nullptr || !mod->running)
            return false;
        loaded_module* mutable_mod = const_cast<loaded_module*>(mod);
        if (release_legacy_module != nullptr)
            release_legacy_module(mutable_mod);
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

    usize count() {
        return used;
    }

    const loaded_module* at(usize i) {
        return i < used ? &modules[i] : nullptr;
    }

    const loaded_module* current_module() {
        return active_module;
    }

} // namespace astia::mod
