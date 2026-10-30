#pragma once

#include <astia/common.hpp>
#include <astia/fs.hpp>
#include <astia/mm/phys_region.hpp>
#include <astia/mm/shptr.hpp>

namespace astia::proc {

    enum class map_protection : u32 {
        none  = 0,
        read  = 1u << 0,
        write = 1u << 1,
        exec  = 1u << 2
    };
    enum class map_flags : u32 {
        none      = 0,
        shared    = 0x01,
        priv      = 0x02,
        fixed     = 0x10,
        anonymous = 0x20
    };
    constexpr map_protection operator|(map_protection a, map_protection b) {
        return static_cast<map_protection>(static_cast<u32>(a) | static_cast<u32>(b));
    }
    constexpr map_protection operator&(map_protection a, map_protection b) {
        return static_cast<map_protection>(static_cast<u32>(a) & static_cast<u32>(b));
    }
    constexpr map_flags operator|(map_flags a, map_flags b) {
        return static_cast<map_flags>(static_cast<u32>(a) | static_cast<u32>(b));
    }
    constexpr map_flags operator&(map_flags a, map_flags b) {
        return static_cast<map_flags>(static_cast<u32>(a) & static_cast<u32>(b));
    }
    constexpr bool has(map_protection v, map_protection f) {
        return (v & f) != map_protection::none;
    }
    constexpr bool has(map_flags v, map_flags f) {
        return (v & f) != map_flags::none;
    }

    /// One contiguous user virtual range and the phys_region backing
    /// it.  An address space keeps these base-sorted; a Copy-on-Write
    /// fault will split one into neighbours (as Linux splits VMAs), so
    /// every entry names exactly one backing region and no page
    /// straddles two of them.
    ///
    /// `holds_ref` marks which entry carries the address space's
    /// reference on the backing: a split shares it, and a removal
    /// hands it to a surviving neighbour (the siblings of a split are
    /// adjacent in the list) so the frames die exactly when the last
    /// mapping of this address space does.
    ///
    /// `file`/`file_offset` give a private file mapping its identity:
    /// the open file description is kept alive by this entry, so the
    /// mapping survives close(), and the offset records where the
    /// range was captured from.  Null/0 for anonymous mappings.
    struct mmap_region {
        mmap_region*     next      = nullptr;
        u64              base      = 0; ///< page-aligned
        usize            pages     = 0;
        map_protection   prot      = map_protection::none;
        bool             shared    = false;
        bool             holds_ref = false;
        mm::phys_region* backing   = nullptr;
        /// First page in `backing` represented by this VMA.  This is
        /// non-zero when an unmap trims the front of a region or when
        /// a shared file mapping starts at a page offset.
        usize                    region_page = 0;
        mm::shptr<fs::open_file> file;
        u64                      file_offset = 0;

        u64 size() const {
            return pages * mm::phys_region::frame_size;
        }
        u64 end() const {
            return base + size();
        }
        bool covers(u64 addr) const {
            return addr >= base && addr < end();
        }
    };

    /// Highest address mmap may hand out: below the TLS block the
    /// userspace loader places at 0x7ffff0000000, well above the ELF
    /// image at 0x40000000 and the stack near the top of the user
    /// half.
    inline constexpr u64 mmap_region_top = 0x00007fffe0000000ull;

} // namespace astia::proc
