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

        void add(u64 base, u64 size, memory_type type) {
            if (_count >= max_regions) {
                return;
            }
            _regions[_count++] = {base, size, type};
        }

        usize count() const {
            return _count;
        }
        const memory_region* begin() const {
            return _regions;
        }
        const memory_region* end() const {
            return _regions + _count;
        }
        const memory_region& operator[](usize index) const {
            return _regions[index];
        }

        private:
        memory_region _regions[max_regions]{};
        usize         _count = 0;
    };

} // namespace astia::mm
