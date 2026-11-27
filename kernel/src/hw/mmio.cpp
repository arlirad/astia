#include <astia/arch/cpu.hpp>
#include <astia/arch/layout.hpp>
#include <astia/arch/mmio.hpp>
#include <astia/arch/page_map.hpp>
#include <astia/arch/spinlock_guard.hpp>
#include <astia/common.hpp>
#include <astia/error.hpp>
#include <astia/hw/mmio.hpp>
#include <astia/hw/pci.hpp>
#include <astia/memory.hpp>
#include <astia/mm/frame_allocator.hpp>

// Hardware-layer implementation.
namespace astia::hw {
    namespace {
        u64            next_mmio = arch::mmio_window().base;
        core::spinlock mmio_lock;
    } // namespace

    mmio_mapping::~mmio_mapping() {
        release();
    }

    mmio_mapping::mmio_mapping(mmio_mapping&& other) noexcept
        : physical_(other.physical_), mapped_base_(other.mapped_base_),
          virtual_base_(other.virtual_base_), offset_(other.offset_),
          mapped_bytes_(other.mapped_bytes_), length_(other.length_), kind_(other.kind_) {
        other.physical_     = 0;
        other.mapped_base_  = 0;
        other.virtual_base_ = 0;
        other.offset_       = 0;
        other.mapped_bytes_ = 0;
        other.length_       = 0;
        other.kind_         = mapping_kind::none;
    }

    mmio_mapping& mmio_mapping::operator=(mmio_mapping&& other) noexcept {
        if (this != &other) {
            release();

            physical_           = other.physical_;
            mapped_base_        = other.mapped_base_;
            virtual_base_       = other.virtual_base_;
            offset_             = other.offset_;
            mapped_bytes_       = other.mapped_bytes_;
            length_             = other.length_;
            kind_               = other.kind_;
            other.physical_     = 0;
            other.mapped_base_  = 0;
            other.virtual_base_ = 0;
            other.offset_       = 0;
            other.mapped_bytes_ = 0;
            other.length_       = 0;
            other.kind_         = mapping_kind::none;
        }

        return *this;
    }

    optional<mmio_mapping> mmio_mapping::map(u64 physical, usz length) {
        if (length == 0 || physical == mm::frame_allocator::invalid_frame ||
            physical > ~u64{0} - static_cast<u64>(length))
            return error_t::einval;

        const u64 offset = physical & mm::page_mask;
        const u64 base   = physical - offset;
        const u64 span   = offset + static_cast<u64>(length);

        if (span < offset || span > ~u64{0} - mm::page_mask)
            return error_t::erange;

        const u64 mapped_bytes = align_up(span, static_cast<u64>(mm::page_size));

        if (mapped_bytes > static_cast<u64>(~usz{0}))
            return error_t::erange;

        auto* page_map = arch::kernel_page_map();
        if (page_map == nullptr)
            return error_t::enosys;

        mmio_mapping                   result;
        arch::interrupt_spinlock_guard guard(mmio_lock);

        const arch::mmio_borrow_status borrow =
            arch::mmio_borrow_status_of(base, mapped_bytes);

        if (borrow == arch::mmio_borrow_status::pci_owned ||
            borrow == arch::mmio_borrow_status::generic_owned ||
            borrow == arch::mmio_borrow_status::aliased ||
            borrow == arch::mmio_borrow_status::out_of_range)
            return error_t::ebusy;

        if (borrow == arch::mmio_borrow_status::borrowable) {
            result.physical_     = physical;
            result.mapped_base_  = base;
            result.virtual_base_ = arch::kernel_base + base;
            result.offset_       = static_cast<usz>(offset);
            result.length_       = length;
            result.kind_         = mapping_kind::borrowed;

            return static_cast<mmio_mapping&&>(result);
        }

        const auto window = arch::mmio_window();

        if (next_mmio > window.limit || mapped_bytes > window.limit - next_mmio)
            return error_t::enomem;

        const u64 virtual_base = next_mmio;

        const arch::mmio_reserve_result reservation =
            arch::mmio_reserve_generic(base, mapped_bytes, virtual_base);

        if (reservation == arch::mmio_reserve_result::busy)
            return error_t::ebusy;

        if (reservation == arch::mmio_reserve_result::full)
            return error_t::enomem;

        if (reservation == arch::mmio_reserve_result::invalid)
            return error_t::erange;

        for (u64 page = 0; page < mapped_bytes; page += mm::page_size) {
            if (!page_map->map_4k(virtual_base + page, base + page,
                                  arch::mmio_device_page_flags())) {
                for (u64 done = 0; done < page; done += mm::page_size) {
                    (void) page_map->unmap(virtual_base + done);
                }

                arch::mmio_release_generic(virtual_base, mapped_bytes);

                return error_t::enomem;
            }
        }

        next_mmio += mapped_bytes;
        result.physical_     = physical;
        result.mapped_base_  = base;
        result.virtual_base_ = virtual_base;
        result.offset_       = static_cast<usz>(offset);
        result.mapped_bytes_ = static_cast<usz>(mapped_bytes);
        result.length_       = length;
        result.kind_         = mapping_kind::generic;

        return static_cast<mmio_mapping&&>(result);
    }

    optional<mmio_mapping> mmio_mapping::map_pci(u64 physical, usz length,
                                                 bool prefetchable) {
        if (length == 0 || physical == mm::frame_allocator::invalid_frame ||
            physical > ~u64{0} - static_cast<u64>(length))
            return error_t::einval;

        const u64 offset = physical & mm::page_mask;
        const u64 base   = physical - offset;
        const u64 span   = offset + static_cast<u64>(length);

        if (span < offset || span > ~u64{0} - mm::page_mask)
            return error_t::erange;

        const u64 mapped_bytes = align_up(span, static_cast<u64>(mm::page_size));

        if (mapped_bytes > static_cast<u64>(~usz{0}))
            return error_t::erange;

        const u64 expected = arch::kernel_base + physical;

        if (expected < arch::kernel_base)
            return error_t::erange;

        u64 virtual_address = 0;

        if (!arch::pci_mmio_map(physical, static_cast<u64>(length), prefetchable,
                                virtual_address))
            return error_t::ebusy;

        if (virtual_address != expected) {
            arch::pci_mmio_unmap(expected, static_cast<u64>(length));

            return error_t::erange;
        }

        mmio_mapping result;

        result.physical_     = physical;
        result.mapped_base_  = base;
        result.virtual_base_ = virtual_address - offset;
        result.offset_       = static_cast<usz>(offset);
        result.mapped_bytes_ = static_cast<usz>(mapped_bytes);
        result.length_       = length;
        result.kind_         = mapping_kind::pci;

        return static_cast<mmio_mapping&&>(result);
    }

    void mmio_mapping::release() {
        if (mapped_base_ == 0)
            return;

        // Borrowed direct-map aliases own no PTE or reservation: skip both.
        if (kind_ == mapping_kind::pci) {
            arch::pci_mmio_unmap(virtual_base_ + offset_, length_);
        }
        else if (kind_ == mapping_kind::generic) {
            auto*                          page_map = arch::kernel_page_map();
            arch::interrupt_spinlock_guard guard(mmio_lock);

            if (page_map != nullptr) {
                for (usz page = 0; page < mapped_bytes_; page += mm::page_size) {
                    (void) page_map->unmap(virtual_base_ + page);
                }
            }

            arch::mmio_release_generic(virtual_base_, mapped_bytes_);
        }

        physical_     = 0;
        mapped_base_  = 0;
        virtual_base_ = 0;
        offset_       = 0;
        mapped_bytes_ = 0;
        length_       = 0;
        kind_         = mapping_kind::none;
    }

    optional<mmio_mapping> pci_bar::map() const {
        if (io)
            return error_t::einval;

        return mmio_mapping::map_pci(address, static_cast<usz>(size), prefetchable);
    }
} // namespace astia::hw
