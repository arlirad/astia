#pragma once

#include <astia/common.hpp>

namespace astia::mm {

    /// Address range types as reported by the firmware/hypervisor map. The
    /// values match the x86/HVM (PVH) ABI, which in turn matches the ACPI
    /// "System Address Map Interfaces" range types.
    enum class memory_type : u32 {
        usable           = 1,
        reserved         = 2,
        acpi_reclaimable = 3,
        acpi_nvs         = 4,
        unusable         = 5,
        disabled         = 6,
        persistent       = 7,
    };

    struct memory_region {
        u64         base = 0;
        u64         size = 0;
        memory_type type = memory_type::reserved;
    };

    /// A fixed-capacity physical memory map. The PVH option ROM stores at
    /// most 128 e820 entries, which we mirror here.
    class memory_map {
        public:
        static constexpr usize max_regions = 128;

        auto add(u64 base, u64 size, memory_type type) -> void {
            if (count_ >= max_regions) {
                return;
            }
            regions_[count_++] = {base, size, type};
        }

        auto count() const -> usize {
            return count_;
        }
        auto begin() const -> const memory_region* {
            return regions_;
        }
        auto end() const -> const memory_region* {
            return regions_ + count_;
        }
        auto operator[](usize index) const -> const memory_region& {
            return regions_[index];
        }

        private:
        memory_region regions_[max_regions]{};
        usize         count_ = 0;
    };

} // namespace astia::mm
