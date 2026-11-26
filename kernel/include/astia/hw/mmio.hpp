#pragma once

#include <astia/common.hpp>
#include <astia/optional.hpp>

namespace astia::hw {
    class mmio_mapping {
        public:
        mmio_mapping() = default;
        ~mmio_mapping();
        mmio_mapping(const mmio_mapping&)            = delete;
        mmio_mapping& operator=(const mmio_mapping&) = delete;
        mmio_mapping(mmio_mapping&& other) noexcept;
        mmio_mapping& operator=(mmio_mapping&& other) noexcept;

        static optional<mmio_mapping> map(u64 physical, usize length);
        static optional<mmio_mapping> map_pci(u64 physical, usize length,
                                              bool prefetchable);

        bool valid() const {
            return mapped_base_ != 0;
        }

        volatile void* base() const {
            return reinterpret_cast<volatile void*>(virtual_base_ + offset_);
        }

        u64 physical() const {
            return physical_;
        }

        usize length() const {
            return length_;
        }

        void release();

        private:
        enum class mapping_kind : u8 {
            none,
            borrowed,
            generic,
            pci,
        };

        u64          physical_     = 0;
        u64          mapped_base_  = 0;
        u64          virtual_base_ = 0;
        usize        offset_       = 0;
        usize        mapped_bytes_ = 0;
        usize        length_       = 0;
        mapping_kind kind_         = mapping_kind::none;
    };
} // namespace astia::hw
