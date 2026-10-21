#include "gdt.hpp"
#include "smp.hpp"
#include <astia/arch/interrupts.hpp>
#include <astia/arch/layout.hpp>
#include <astia/arch/page_map.hpp>
#include <astia/boot.hpp>
#include <astia/init.hpp>
#include <astia/memory.hpp>
#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/heap.hpp>

// Kernel image bounds from the linker script; both are *physical* addresses
// (see cmake/linker/virt-x86_64.ld): the frame allocator reserves
// [__kernel_start, __kernel_end) and parks its bitmap right after it.
extern "C" char __kernel_start[];
extern "C" char __kernel_end[];

namespace astia::arch {

    namespace {

        /// The kernel's own address space, built once by the paging step.
        page_map kernel_map;
        bool     kernel_map_live = false;

        /// Page-mapper handed to mm::heap, so heap growth can create
        /// virtual->physical mappings without mm ever seeing a page_map.
        bool map_heap_page(u64 virt, u64 phys) {
            return kernel_map.map_4k(virt, phys,
                                     page_flags::present | page_flags::writable);
        }

        void* direct_access(u64 physical) {
            return reinterpret_cast<void*>(kernel_base + physical);
        }

        /// Top of usable RAM as reported by the loader.
        u64 usable_top(const mm::memory_map& map) {
            u64 top = 0;
            for (const auto& region : map) {
                if (region.type == mm::memory_type::usable &&
                    region.base + region.size > top) {
                    top = region.base + region.size;
                }
            }
            return top;
        }

    } // namespace

    page_map* kernel_page_map() {
        return kernel_map_live ? &kernel_map : nullptr;
    }

} // namespace astia::arch

namespace astia::arch::x86_64 {

    static bool step_paging(init::context& ctx) {
        if (ctx.info == nullptr) {
            return false;
        }

        page_map map = page_map::create();
        if (!map.valid()) {
            return false;
        }

        constexpr u64        two_m = 2ull << 20;
        constexpr u64        one_g = 1ull << 30;
        constexpr page_flags rw    = page_flags::present | page_flags::writable;

        // Identity window [0, 1 GiB), mirroring what boot.S established:
        // everything still points into physical memory — the boot stack,
        // the frame bitmap, loaded modules, the loader's start info, and
        // the page-table frames themselves (page_map writes tables through
        // this window).
        for (u64 v = 0; v < one_g; v += two_m) {
            if (!map.map_2m(v, v, rw)) {
                return false;
            }
        }

        // Direct physical map: kernel_base + p -> p, up to the top of usable
        // RAM (in 2 MiB pages; reserved gaps inside the window are mapped
        // but only ever touched on demand).
        const u64 top = align_up(usable_top(ctx.info->memory), two_m);
        for (u64 p = 0; p < top; p += two_m) {
            if (!map.map_2m(kernel_base + p, p, rw)) {
                return false;
            }
        }

        // The kernel image at image_base + physical address, one page at a
        // time (the image is not 2 MiB aligned).
        const u64 phys_start = reinterpret_cast<u64>(__kernel_start);
        const u64 phys_end   = reinterpret_cast<u64>(__kernel_end);
        for (u64 p = phys_start; p < phys_end; p += 4096) {
            if (!map.map_4k(image_base + p, p, rw)) {
                return false;
            }
        }

        // Publish the heap window: it starts right after the image in the
        // image window and gets its frames + mappings in the heap step.
        const u64 heap_base = image_base + align_up(phys_end, 4096);
        mm::heap::configure(reinterpret_cast<void*>(heap_base), mm::heap::region_bytes,
                            map_heap_page);

        kernel_map      = static_cast<page_map&&>(map);
        kernel_map_live = true;
        kernel_map.activate();
        mm::kernel_frames().configure_access(direct_access);
        return true;
    }

    /// Arm the IDT, remap the PICs and start the PIT tick. Runs after
    /// paging (the IDT and handler stubs live in the kernel image, which
    /// the paging step maps) and leaves interrupts masked — kmain opens
    /// the gate once its selftest has proven the dispatch path works.
    static bool step_interrupts(init::context&) {
        return init_interrupts();
    }

    static bool step_gdt_tss(init::context&) {
        return init_gdt_tss();
    }

    static bool step_smp(init::context& ctx) {
        return ctx.info != nullptr && smp::init(*ctx.info);
    }

} // namespace astia::arch::x86_64

ASTIA_INIT_STEP(memory, paging, astia::arch::x86_64::step_paging, "frames")
ASTIA_INIT_STEP(devices, gdt_tss, astia::arch::x86_64::step_gdt_tss, "heap")
ASTIA_INIT_STEP(devices, interrupts, astia::arch::x86_64::step_interrupts, "paging",
                "gdt_tss")
ASTIA_INIT_STEP(devices, smp, astia::arch::x86_64::step_smp, "boot_info", "gdt_tss",
                "interrupts")
