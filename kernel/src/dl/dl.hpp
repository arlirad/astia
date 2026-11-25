#pragma once

#include <astia/common.hpp>
#include <astia/core/task.hpp>
#include <astia/elf.hpp>
#include <astia/mapping.hpp>
#include <astia/mm/shptr.hpp>
#include <astia/optional.hpp>

#include "proc/mmap_region.hpp"

namespace astia::proc {
    class process;
}

namespace astia::dl {

    /// ELF64 `p_flags` bits (PF_X/PF_W/PF_R), kept typed so the ABI's
    /// PROT_* order is never confused with it.
    enum class elf_segment_flag : u32 {
        execute = 1u << 0,
        write   = 1u << 1,
        read    = 1u << 2,
    };
    constexpr elf_segment_flag operator|(elf_segment_flag a, elf_segment_flag b) {
        return static_cast<elf_segment_flag>(static_cast<u32>(a) | static_cast<u32>(b));
    }
    constexpr bool has(elf_segment_flag value, elf_segment_flag bit) {
        return (static_cast<u32>(value) & static_cast<u32>(bit)) != 0;
    }

    /// One PT_LOAD segment of a cached image, relative to the load bias.
    struct segment {
        u64                  vaddr       = 0; ///< bias-relative, page-aligned
        u64                  bytes       = 0;
        proc::map_protection prot        = proc::map_protection::none;
        u32                  region_page = 0; ///< first page in `image::region`
    };

    /// One file image shared by every process that maps it.  The single
    /// `region` covers the loader's page-aligned span: read-only segments
    /// are shared as they are, writable ones are mapped read-only and
    /// copied on first write.
    struct image {
        mm::phys_region* region        = nullptr;
        u64              base          = 0; ///< load bias and dlunmap handle
        u64              span          = 0;
        u64              entry         = 0;
        u64              phdr          = 0;
        u64              ident_mount   = 0;
        u64              ident_inode   = 0;
        u64              ident_size    = 0;
        u32              phnum         = 0;
        u32              phent         = 0;
        u32              segment_count = 0;
        u32              map_count     = 0; ///< address spaces mapping it
        bool             loading       = false;
        segment          segments[elf::loader::max_segments]{};

        ~image();
    };

    /// What dlmap hands back; the ABI copy lives in <astia/api/syscalls.h>.
    struct info {
        u64 base  = 0;
        u64 span  = 0;
        u64 entry = 0;
        u64 phdr  = 0;
        u32 phnum = 0;
        u32 phent = 0;
    };

    /// Register the fork/teardown hooks.  Runs as its own init step after
    /// process startup, so `proc` never has to know this subsystem exists.
    bool init();

    core::task<info> map(proc::process& target, fd_t fd, u32 flags);
    i64              unmap(proc::process& target, u64 base);
    void             release(proc::process& target);
    bool             clone(proc::process& child, proc::process& parent);

} // namespace astia::dl

namespace astia {

    /// Syscall/library boundary mapping for ELF `p_flags`.  All three bits
    /// are meaningful; anything else is a malformed image.
    template <>
    struct mapper<dl::elf_segment_flag> {
        static optional<dl::elf_segment_flag> from(i64 raw) {
            if (raw < 0 || raw > 7)
                return optional<dl::elf_segment_flag>(error_t::einval);
            return optional<dl::elf_segment_flag>(
                static_cast<dl::elf_segment_flag>(static_cast<u32>(raw)));
        }
    };

} // namespace astia
