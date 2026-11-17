#include <astia/dev/mmio.hpp>
#include <astia/dev/pci.hpp>

#include <astia/arch/cpu.hpp>
#include <astia/arch/layout.hpp>
#include <astia/arch/mmio.hpp>
#include <astia/arch/page_map.hpp>
#include <astia/common.hpp>
#include <astia/error.hpp>
#include <astia/memory.hpp>
#include <astia/mm/frame_allocator.hpp>

namespace astia::dev {

    namespace {
        u64           next_mmio = arch::mmio_window().base;
        core::spinlock mmio_lock;
    } // namespace

    mmio_mapping::~mmio_mapping() {
        release();
    }

    mmio_mapping::mmio_mapping(mmio_mapping&& other) noexcept
        : physical_(other.physical_), mapped_base_(other.mapped_base_),
          virtual_base_(other.virtual_base_), offset_(other.offset_),
          mapped_bytes_(other.mapped_bytes_), length_(other.length_) {
        other.physical_     = 0;
        other.mapped_base_  = 0;
        other.virtual_base_ = 0;
        other.offset_       = 0;
        other.mapped_bytes_ = 0;
        other.length_       = 0;
    }

    mmio_mapping& mmio_mapping::operator=(mmio_mapping&& other) noexcept {
        if (this != &other) {
            release();
            physical_     = other.physical_;
            mapped_base_  = other.mapped_base_;
            virtual_base_ = other.virtual_base_;
            offset_       = other.offset_;
            mapped_bytes_ = other.mapped_bytes_;
            length_       = other.length_;
            other.physical_     = 0;
            other.mapped_base_  = 0;
            other.virtual_base_ = 0;
            other.offset_       = 0;
            other.mapped_bytes_ = 0;
            other.length_       = 0;
        }
        return *this;
    }

    optional<mmio_mapping> mmio_mapping::map(u64 physical, usize length) {
        if (length == 0 || physical == mm::frame_allocator::invalid_frame ||
            physical > ~u64{0} - static_cast<u64>(length)) {
            return error_t::einval;
        }
        const u64 offset = physical & mm::page_mask;
        const u64 base   = physical - offset;
        const u64 span   = offset + static_cast<u64>(length);
        if (span < offset || span > ~u64{0} - mm::page_mask) {
            return error_t::erange;
        }
        const u64 mapped_bytes = align_up(span, static_cast<u64>(mm::page_size));
        if (mapped_bytes > static_cast<u64>(~usize{0})) {
            return error_t::erange;
        }

        auto* page_map = arch::kernel_page_map();
        if (page_map == nullptr) {
            return error_t::enosys;
        }

#ifndef ASTIA_HOST_TEST
        if (base <= ~u64{0} - arch::kernel_base &&
            page_map->translate(arch::kernel_base + base) == base) {
            mmio_mapping result;
            result.physical_ = physical;
            result.mapped_base_ = base;
            result.virtual_base_ = arch::kernel_base + base;
            result.offset_ = static_cast<usize>(offset);
            result.length_ = length;
            return static_cast<mmio_mapping&&>(result);
        }
#endif

        mmio_mapping result;
        arch::interrupt_spinlock_guard guard(mmio_lock);
        const auto window = arch::mmio_window();
        if (next_mmio > window.limit || mapped_bytes > window.limit - next_mmio) {
            return error_t::enomem;
        }
        const u64 virtual_base = next_mmio;
        next_mmio += mapped_bytes;
        for (u64 page = 0; page < mapped_bytes; page += mm::page_size) {
            if (!page_map->map_4k(virtual_base + page, base + page,
                                  arch::page_flags::present | arch::page_flags::writable |
                                      arch::page_flags::cache_disable)) {
                for (u64 done = 0; done < page; done += mm::page_size)
                    (void) page_map->unmap(virtual_base + done);
                return error_t::enomem;
            }
        }
        result.physical_     = physical;
        result.mapped_base_  = base;
        result.virtual_base_ = virtual_base;
        result.offset_       = static_cast<usize>(offset);
        result.mapped_bytes_ = static_cast<usize>(mapped_bytes);
        result.length_       = length;
        return static_cast<mmio_mapping&&>(result);
    }

    void mmio_mapping::release() {
        if (mapped_base_ == 0) {
            return;
        }
        auto* page_map = arch::kernel_page_map();
        if (page_map != nullptr) {
            arch::interrupt_spinlock_guard guard(mmio_lock);
            for (usize page = 0; page < mapped_bytes_; page += mm::page_size)
                (void) page_map->unmap(virtual_base_ + page);
        }
        physical_     = 0;
        mapped_base_  = 0;
        virtual_base_ = 0;
        offset_       = 0;
        mapped_bytes_ = 0;
        length_       = 0;
    }

    optional<mmio_mapping> pci_bar::map() const {
        if (io || address == 0 || size == 0 || address > ~u64{0} - size)
            return error_t::einval;
        return mmio_mapping::map(address, static_cast<usize>(size));
    }

} // namespace astia::dev
