// Module registry: the policy layer over the pure ELF loader.
//
// Memory comes from the kernel frame allocator; undefined symbols resolve
// against the export table (exports.cpp). Both are wired in here as plain
// callbacks — no dependency injection, the loader just takes function pointers.

#include "module.hpp"

#include "elf.hpp"
#include "exports.hpp"
#include "frame_allocator.hpp"

namespace astia::mod {

    namespace {

        module modules[max_modules] = {};
        usize  used                 = 0;

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

        /// Backs the loader's undefined-symbol resolution with the export
        /// table. Signature must match elf::resolve_fn.
        u64 image_resolve(void*, const char* name) {
            return resolve(name);
        }

    } // namespace

    const module* load_module(const void* image, usize size) {
        if (image == nullptr || size == 0 || used >= max_modules) {
            return nullptr;
        }

        elf::env e{};
        e.ctx     = nullptr;
        e.map     = image_map;
        e.resolve = image_resolve;

        elf::image img;
        if (!elf::load(image, size, e, img)) {
            return nullptr;
        }

        module& slot = modules[used];
        slot.name    = "module";
        slot.base    = img.base;
        slot.size    = img.size;
        slot.entry   = reinterpret_cast<int (*)()>(img.entry);
        slot.running = false;
        ++used;
        return &slot;
    }

    bool unload_module(const module* mod) {
        if (mod == nullptr) {
            return false;
        }
        for (usize i = 0; i < used; ++i) {
            if (&modules[i] != mod) {
                continue;
            }
            // The loader makes one contiguous allocation, so it is one
            // contiguous release.
            using fa     = mm::frame_allocator;
            usize frames = (mod->size + fa::frame_size - 1) / fa::frame_size;
            mm::kernel_frames().release(reinterpret_cast<u64>(mod->base), frames);
            // Compact the registry.
            for (usize j = i; j + 1 < used; ++j) {
                modules[j] = modules[j + 1];
            }
            --used;
            return true;
        }
        return false;
    }

    int run_module(const module* mod) {
        if (mod == nullptr || mod->running || mod->entry == nullptr) {
            return -1;
        }
        const_cast<module*>(mod)->running = true;
        return mod->entry();
    }

    usize count() {
        return used;
    }

    const module* at(usize i) {
        return i < used ? &modules[i] : nullptr;
    }

} // namespace astia::mod
