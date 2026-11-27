#include <astia/arch/interrupts.hpp>
#include <astia/arch/layout.hpp>
#include <astia/arch/mmio.hpp>
#include <astia/arch/page_map.hpp>
#include <astia/boot.hpp>
#include <astia/hw/pci.hpp>
#include <astia/init.hpp>
#include <astia/memory.hpp>
#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/heap.hpp>
#include <astia/mm/memory_map.hpp>

#include "gdt.hpp"
#include "pci_mmio_owner.hpp"
#include "smp.hpp"

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

        u64 direct_physical(const void* address) {
            return reinterpret_cast<u64>(address) - kernel_base;
        }
    } // namespace

    page_map* kernel_page_map() {
        return kernel_map_live ? &kernel_map : nullptr;
    }
} // namespace astia::arch

namespace astia::arch::x86_64 {
    static bool step_paging(init::context& ctx) {
        if (!ctx.info)
            return false;

        page_map map = page_map::create();

        if (!map.valid())
            return false;

        constexpr u64 two_m = 2ull << 20;

        constexpr page_flags rw = page_flags::present | page_flags::writable;

        // Identity window [0, 1 GiB), mirroring what boot.S established:
        // everything still points into physical memory — the boot stack,
        // the frame bitmap, loaded modules, the loader's start info, and
        // the page-table frames themselves (page_map writes tables through
        // this window).
        for (u64 v = 0; v < pci_identity_window_size; v += two_m) {
            if (!map.map_2m(v, v, rw))
                return false;
        }

        // Direct physical map: kernel_base + p -> p, up to the top of usable
        // RAM (in 2 MiB pages; reserved gaps inside the window are mapped
        // but only ever touched on demand). Reserved firmware above usable
        // RAM is read through the ACPI reader's on-demand mapping instead.
        u64 usable_top = 0;

        if (!mm::usable_top(ctx.info->memory, usable_top))
            return false;

        const u64 top = align_up(usable_top, two_m);

        for (u64 p = 0; p < top; p += two_m) {
            if (!map.map_2m(kernel_base + p, p, rw))
                return false;
        }

        if (ctx.info->framebuffer.valid()) {
            const u64 length = static_cast<u64>(ctx.info->framebuffer.pitch) *
                               ctx.info->framebuffer.height;

            if (length == 0 || length > ~u64{0} - ctx.info->framebuffer.physical_address)
                return false;

            const u64 first = ctx.info->framebuffer.physical_address & ~mm::page_mask;
            const u64 end   = align_up(ctx.info->framebuffer.physical_address + length,
                                       static_cast<u64>(mm::page_size));
            if (end < first)
                return false;

            for (u64 p = first; p < end; p += mm::page_size) {
                if (!map.map_4k(kernel_base + p, p, rw | page_flags::cache_disable))
                    return false;
            }
        }

        // The kernel image at image_base + physical address, one page at a
        // time (the image is not 2 MiB aligned).
        const u64 phys_start = reinterpret_cast<u64>(__kernel_start);
        const u64 phys_end   = reinterpret_cast<u64>(__kernel_end);

        for (u64 p = phys_start; p < phys_end; p += mm::page_size) {
            if (!map.map_4k(image_base + p, p, rw))
                return false;
        }

        // Publish the heap window: it starts right after the image in the
        // image window and gets its frames + mappings in the heap step.
        const u64 heap_base = image_base + align_up(phys_end, mm::page_size);

        mm::heap::configure(reinterpret_cast<void*>(heap_base), mm::heap::region_bytes,
                            map_heap_page);

        kernel_map      = static_cast<page_map&&>(map);
        kernel_map_live = true;

        kernel_map.activate();
        mm::kernel_frames().configure_access(direct_access, direct_physical);
        pci_mmio_owner_configure(pci_mmio_safe_floor(top),
                                 pci_mmio_va_limits_for(kernel_base, direct_map_limit,
                                                        mmio_window().base,
                                                        recursive_base, image_base));
        hw::pci_set_mmio_mapper(pci_mmio_owner_map, pci_mmio_owner_unmap);

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

namespace astia::arch {
    bool init_scheduler() {
        return x86_64::smp::init_scheduler();
    }

    mmio_borrow_status mmio_borrow_status_of(u64 base, u64 mapped_bytes) {
        return x86_64::pci_mmio_owner_borrow_status(base, mapped_bytes);
    }

    mmio_reserve_result mmio_reserve_generic(u64 base, u64 mapped_bytes,
                                             u64 virtual_address) {
        return x86_64::pci_mmio_owner_reserve_generic(base, mapped_bytes,
                                                      virtual_address);
    }

    void mmio_release_generic(u64 virtual_address, u64 mapped_bytes) {
        x86_64::pci_mmio_owner_release_generic(virtual_address, mapped_bytes);
    }

    bool pci_mmio_map(u64 physical, u64 size, bool prefetchable, u64& virtual_address) {
        return x86_64::pci_mmio_owner_map(physical, size, prefetchable, virtual_address);
    }

    void pci_mmio_unmap(u64 virtual_address, u64 size) {
        x86_64::pci_mmio_owner_unmap(virtual_address, size);
    }
} // namespace astia::arch

ASTIA_INIT_STEP(memory, paging, astia::arch::x86_64::step_paging, "frames")
ASTIA_INIT_STEP(devices, gdt_tss, astia::arch::x86_64::step_gdt_tss, "heap")
ASTIA_INIT_STEP(devices, interrupts, astia::arch::x86_64::step_interrupts, "paging",
                "gdt_tss")
ASTIA_INIT_STEP(devices, smp, astia::arch::x86_64::step_smp, "boot_info", "gdt_tss",
                "interrupts")
