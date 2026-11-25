#include "internal.hpp"

#include <new>

namespace astia::elf::loader {

    using namespace detail;

    // ========================================================================
    //  Linked executable (ET_EXEC / ET_DYN) loader
    // ========================================================================

    namespace {

        struct async_mapping {
            const async_env& env;
            void*            address;
            usize            size;

            void reset() {
                if (address != nullptr) {
                    env.unmap(env.ctx, address, size);
                    address = nullptr;
                }
            }

            void dismiss() {
                address = nullptr;
            }

            ~async_mapping() {
                reset();
            }
        };

        constexpr usize stream_read_chunk_size = 4096;
        constexpr usize stream_rela_chunk_size = sizeof(elf64_rela) * 170;

        struct loader_scratch {
            elf64_phdr ph[max_program_headers]{};
            u8         buffer[stream_read_chunk_size]{};
            u8         zero[stream_read_chunk_size]{};
            u8         rela_buffer[stream_rela_chunk_size]{};
        };

        struct loader_scratch_owner {
            loader_scratch* value = nullptr;

            ~loader_scratch_owner() {
                delete value;
            }
        };

        bool validate_load_phdr(const elf64_phdr& ph, usize file_size,
                                u64& map_alignment) {
            if (ph.p_memsz < ph.p_filesz || ph.p_vaddr > ~u64{0} - ph.p_memsz ||
                !in_bounds(ph.p_offset, ph.p_filesz, file_size))
                return false;
            if (ph.p_align > 1) {
                if (!is_power_of_two(ph.p_align) ||
                    (ph.p_vaddr & (ph.p_align - 1)) != (ph.p_offset & (ph.p_align - 1)))
                    return false;
                if (ph.p_align > static_cast<u64>(~usize{0}))
                    return false;
                if (ph.p_align > map_alignment)
                    map_alignment = ph.p_align;
            }
            return true;
        }

        bool load_bias_alignment_ok(const elf64_phdr* ph, u16 phnum, u64 mapped_addr,
                                    u64 span_start) {
            for (u16 i = 0; i < phnum; ++i) {
                if (ph[i].p_type == pt_load && ph[i].p_align > 1 &&
                    (mapped_addr & (ph[i].p_align - 1)) !=
                        (span_start & (ph[i].p_align - 1)))
                    return false;
            }
            return true;
        }

        bool vaddr_range_to_off(const elf64_phdr* ph, u16 phnum, u64 vaddr, u64 size,
                                usize len, u64& off) {
            for (u16 i = 0; i < phnum; ++i) {
                if (ph[i].p_type != pt_load ||
                    !in_bounds(ph[i].p_offset, ph[i].p_filesz, len) ||
                    vaddr < ph[i].p_vaddr || vaddr - ph[i].p_vaddr > ph[i].p_filesz ||
                    size > ph[i].p_filesz - (vaddr - ph[i].p_vaddr))
                    continue;
                off = ph[i].p_offset + (vaddr - ph[i].p_vaddr);
                return true;
            }
            return false;
        }

        bool vaddr_range_in_load(const elf64_phdr* ph, u16 phnum, u64 vaddr, u64 size) {
            for (u16 i = 0; i < phnum; ++i) {
                if (ph[i].p_type != pt_load || vaddr < ph[i].p_vaddr ||
                    vaddr - ph[i].p_vaddr > ph[i].p_memsz ||
                    size > ph[i].p_memsz - (vaddr - ph[i].p_vaddr))
                    continue;
                return true;
            }
            return false;
        }

        /// Inverse of vaddr_range_to_off: the in-memory virtual address a
        /// file range is loaded at (AT_PHDR for the startup vector).
        bool offset_range_to_vaddr(const elf64_phdr* ph, u16 phnum, u64 off, u64 size,
                                   u64& vaddr) {
            for (u16 i = 0; i < phnum; ++i) {
                if (ph[i].p_type != pt_load || off < ph[i].p_offset ||
                    off - ph[i].p_offset > ph[i].p_filesz ||
                    size > ph[i].p_filesz - (off - ph[i].p_offset))
                    continue;
                vaddr = ph[i].p_vaddr + (off - ph[i].p_offset);
                return true;
            }
            return false;
        }

        /// Record the phdr table address the image exposes.
        void fill_image_tables(const elf64_phdr* ph, u16 phnum, u64 phoff, u64 phbytes,
                               u64 bias, image& out) {
            out.phnum      = phnum;
            out.phent      = sizeof(elf64_phdr);
            u64 phdr_vaddr = 0;
            if (offset_range_to_vaddr(ph, phnum, phoff, phbytes, phdr_vaddr) &&
                phdr_vaddr <= ~u64{0} - bias)
                out.phdr_vaddr = phdr_vaddr + bias;
        }

        // Translate a virtual address to a file offset using the PT_LOAD
        // segments (the only segments whose bytes live in the file).
        bool vaddr_to_off(const elf64_phdr* ph, u16 phnum, u64 vaddr, usize len,
                          u64& off) {
            for (u16 i = 0; i < phnum; ++i) {
                if (ph[i].p_type != pt_load) {
                    continue;
                }
                if (vaddr < ph[i].p_vaddr)
                    continue;
                const u64 delta = vaddr - ph[i].p_vaddr;
                if (delta >= ph[i].p_filesz ||
                    !in_bounds(ph[i].p_offset, ph[i].p_filesz, len) ||
                    delta > len - ph[i].p_offset)
                    continue;
                off = ph[i].p_offset + delta;
                return true;
            }
            return false;
        }

        // Apply the R_X86_64_RELATIVE dynamic relocations named by .dynamic.
        // A PIE's link-time absolute pointers are stored link-time-relative;
        // each needs `*loc = bias + addend` to be usable at its run address.
        // Any other dynamic relocation type means real dynamic linking, which
        // this loader deliberately does not do, so we reject it.
        bool apply_relative_relocs(span<const u8> bytes, const elf64_phdr* ph, u16 phnum,
                                   u64 mapped_addr, u64 span_start, u64 span_end) {
            const usize len = bytes.size();
            // Find PT_DYNAMIC; a fully static ET_EXEC has none, which is fine.
            const elf64_dyn* dyn       = nullptr;
            u64              dyn_count = 0;
            for (u16 i = 0; i < phnum; ++i) {
                if (ph[i].p_type == pt_dynamic) {
                    if (ph[i].p_filesz == 0 || ph[i].p_filesz % sizeof(elf64_dyn) != 0 ||
                        !in_bounds(ph[i].p_offset, ph[i].p_filesz, len)) {
                        return false;
                    }
                    dyn =
                        reinterpret_cast<const elf64_dyn*>(bytes.data() + ph[i].p_offset);
                    dyn_count = ph[i].p_filesz / sizeof(elf64_dyn);
                    break;
                }
            }
            if (dyn == nullptr) {
                return true; // nothing to relocate
            }

            // Locate DT_RELA (a virtual address) and its size.
            u64  rela_vaddr = 0;
            u64  rela_size  = 0;
            bool rela_tag   = false;
            bool relasz_tag = false;
            bool terminated = false;
            for (u64 i = 0; i < dyn_count; ++i) {
                if (dyn[i].d_tag == dt_null) {
                    terminated = true;
                    break;
                }
                if (dyn[i].d_tag == dt_rela) {
                    rela_vaddr = dyn[i].d_val;
                    rela_tag   = true;
                }
                else if (dyn[i].d_tag == dt_relasz) {
                    rela_size  = dyn[i].d_val;
                    relasz_tag = true;
                }
                else if (dyn[i].d_tag == dt_relaent) {
                    if (dyn[i].d_val != sizeof(elf64_rela)) {
                        return false; // unexpected RELA entry size
                    }
                }
            }
            if (!terminated || rela_tag != relasz_tag ||
                rela_size % sizeof(elf64_rela) != 0 ||
                (rela_size != 0 && rela_vaddr == 0) ||
                (rela_size == 0 && rela_vaddr != 0)) {
                return false;
            }
            if (rela_size == 0) {
                return true; // no dynamic relocations
            }

            u64 rela_off = 0;
            if (!vaddr_to_off(ph, phnum, rela_vaddr, len, rela_off)) {
                return false;
            }
            if (!in_bounds(rela_off, rela_size, len)) {
                return false;
            }
            u64       count = rela_size / sizeof(elf64_rela);
            const u8* rela  = bytes.data() + rela_off;

            for (u64 k = 0; k < count; ++k) {
                const u8* entry  = rela + k * sizeof(elf64_rela);
                const u64 r_info = read_u64_bytes(entry + 8);
                const u32 type   = r_type(r_info);
                if (type == r_x86_64_none) {
                    continue;
                }
                if (type != r_x86_64_relative) {
                    return false; // needs full dynamic linking, which we do not do
                }
                // The patched slot lives in the mapped image at vaddr + bias.
                u64 loc_vaddr = read_u64_bytes(entry);
                if (loc_vaddr < span_start || loc_vaddr > span_end ||
                    sizeof(u64) > span_end - loc_vaddr ||
                    !vaddr_range_in_load(ph, phnum, loc_vaddr, sizeof(u64)) ||
                    loc_vaddr - span_start > ~u64{0} - mapped_addr) {
                    return false;
                }
                const u64 addend = read_u64_bytes(entry + 16);
                u64       value  = 0;
                if (!checked_biased_value(mapped_addr, span_start,
                                          static_cast<i64>(addend), value))
                    return false;
                write_u64(reinterpret_cast<u8*>(mapped_addr + (loc_vaddr - span_start)),
                          value);
            }
            return true;
        }

    } // namespace

    bool load_executable(span<const u8> bytes, const env& e, image& out) {
        const usize len = bytes.size();
        if (e.map == nullptr || e.unmap == nullptr) {
            return false;
        }
        u16 type = 0;
        if (!ident_type(bytes, type) || (type != et_exec && type != et_dyn)) {
            return false;
        }
        const auto* eh         = reinterpret_cast<const elf64_ehdr*>(bytes.data());
        const u8*   base_bytes = bytes.data();

        if (eh->e_ehsize != sizeof(elf64_ehdr) || eh->e_phnum == 0 ||
            eh->e_phnum > max_program_headers || eh->e_phentsize != sizeof(elf64_phdr)) {
            return false;
        }
        if (!in_bounds(eh->e_phoff, static_cast<u64>(eh->e_phnum) * sizeof(elf64_phdr),
                       bytes.size())) {
            return false;
        }
        const auto* ph    = reinterpret_cast<const elf64_phdr*>(base_bytes + eh->e_phoff);
        u16         phnum = eh->e_phnum;

        // --- Pass 1: collect PT_LOAD segments and the total span they cover.
        // Mapped as one contiguous reservation so relative vaddr layout is
        // preserved; the load bias shifts the whole set at once.
        u64   min_vaddr     = ~u64{0};
        u64   max_end       = 0;
        u64   map_alignment = page_size;
        usize nsegs         = 0;
        for (u16 i = 0; i < phnum; ++i) {
            if (ph[i].p_type != pt_load) {
                continue;
            }
            if (nsegs >= max_segments) {
                return false; // too many segments for our fixed cap
            }
            if (!validate_load_phdr(ph[i], len, map_alignment)) {
                return false;
            }
            if (ph[i].p_vaddr < min_vaddr) {
                min_vaddr = ph[i].p_vaddr;
            }
            const u64 end = ph[i].p_vaddr + ph[i].p_memsz;
            if (end > max_end)
                max_end = end;
            ++nsegs;
        }
        if (nsegs == 0 || min_vaddr == ~u64{0} || max_end <= min_vaddr) {
            return false;
        }

        if (max_end > ~u64{0} - (page_size - 1))
            return false;
        u64 span_start = align_down(min_vaddr);
        u64 span_end   = align_up(max_end);
        if (span_end <= span_start || span_end - span_start > static_cast<u64>(~usize{0}))
            return false;
        u64 span = span_end - span_start;

        // --- Reserve the span. ET_EXEC is linked at fixed addresses and must
        // land exactly there; ET_DYN is position-independent, so the allocator
        // picks the base and we derive the bias from it.
        bool  fixed = (type == et_exec);
        void* mapped =
            e.map(e.ctx, span_start, static_cast<usize>(span),
                  static_cast<usize>(fixed ? page_size : map_alignment), fixed);
        if (mapped == nullptr) {
            return false;
        }
        struct mapped_block {
            const env& environment;
            void*      address;
            usize      size;

            ~mapped_block() {
                if (address != nullptr)
                    environment.unmap(environment.ctx, address, size);
            }

            void keep() {
                address = nullptr;
            }
        } mapping{e, mapped, static_cast<usize>(span)};
        u64 mapped_addr = reinterpret_cast<u64>(mapped);
        if (fixed && mapped_addr != span_start) {
            return false; // fixed-address map ignored the requested address
        }
        if (!load_bias_alignment_ok(ph, phnum, mapped_addr, span_start))
            return false;
        if (span > ~u64{0} - mapped_addr)
            return false;
        u64 load_bias = mapped_addr - span_start;

        if (e.copy == nullptr) {
            auto* zero = static_cast<u8*>(mapped);
            for (u64 i = 0; i < span; ++i)
                zero[i] = 0;
        }
        else {
            u8  zero[4096] = {};
            u64 cleared    = 0;
            while (cleared < span) {
                const usize length = span - cleared < sizeof(zero)
                                         ? static_cast<usize>(span - cleared)
                                         : sizeof(zero);
                if (!e.copy(e.ctx, mapped_addr + cleared,
                            astia::span<const u8>(zero, length)))
                    return false;
                cleared += length;
            }
        }

        // --- Pass 2: copy each segment's file bytes into place.
        usize seg_i = 0;
        for (u16 i = 0; i < phnum; ++i) {
            if (ph[i].p_type != pt_load) {
                continue;
            }
            if (!in_bounds(ph[i].p_offset, ph[i].p_filesz, len)) {
                return false;
            }
            if (ph[i].p_vaddr < span_start ||
                ph[i].p_vaddr - span_start > span - ph[i].p_memsz ||
                ph[i].p_vaddr - span_start > ~u64{0} - mapped_addr)
                return false;
            u64       dest = mapped_addr + (ph[i].p_vaddr - span_start);
            const u8* src  = base_bytes + ph[i].p_offset;
            u8*       d    = nullptr;
            for (usize mapped_i = 0; mapped_i < seg_i; ++mapped_i) {
                const segment& mapped_seg = out.segs[mapped_i];
                if (dest >= reinterpret_cast<u64>(mapped_seg.loc) &&
                    dest < reinterpret_cast<u64>(mapped_seg.loc) + mapped_seg.memsz) {
                    d = static_cast<u8*>(mapped_seg.loc) +
                        (dest - reinterpret_cast<u64>(mapped_seg.loc));
                    break;
                }
            }
            if (e.copy != nullptr) {
                if (!e.copy(
                        e.ctx, dest,
                        astia::span<const u8>(src, static_cast<usize>(ph[i].p_filesz))))
                    return false;
            }
            else if (d == nullptr) {
                d = reinterpret_cast<u8*>(dest);
            }
            if (e.copy == nullptr)
                for (u64 k = 0; k < ph[i].p_filesz; ++k)
                    d[k] = src[k];
            segment& seg = out.segs[seg_i++];
            seg.vaddr    = ph[i].p_vaddr;
            seg.filesz   = ph[i].p_filesz;
            seg.memsz    = ph[i].p_memsz;
            seg.flags    = ph[i].p_flags;
            seg.loc      = reinterpret_cast<void*>(dest);
        }

        // --- Apply R_X86_64_RELATIVE dynamic relocations (a PIE needs them to
        // make its internal pointers valid at the run address).
        if (!apply_relative_relocs(bytes, ph, phnum, mapped_addr, span_start, span_end)) {
            return false;
        }

        // --- Static TLS: record the PT_TLS template for whoever
        // materializes it per thread (the userspace loader).
        bool tls_seen = false;
        for (u16 i = 0; i < phnum; ++i) {
            if (ph[i].p_type != pt_tls) {
                continue;
            }
            if (tls_seen)
                return false;
            tls_seen = true;
            if (ph[i].p_filesz > ph[i].p_memsz ||
                !in_bounds(ph[i].p_offset, ph[i].p_filesz, len) ||
                (ph[i].p_align != 0 && !is_power_of_two(ph[i].p_align)) ||
                ph[i].p_memsz > static_cast<u64>(~usize{0}))
                return false;
            if (ph[i].p_memsz != 0) {
                const u64 align    = ph[i].p_align == 0 ? 1 : ph[i].p_align;
                u64       tls_span = 0;
                if (!checked_align_up(ph[i].p_memsz, align, tls_span) ||
                    tls_span > static_cast<u64>(~usize{0}) - page_size)
                    return false;
                out.tls_offset = ph[i].p_offset;
                out.tls_filesz = ph[i].p_filesz;
                out.tls_memsz  = ph[i].p_memsz;
                out.tls_align  = ph[i].p_align;
            }
            break;
        }

        if (eh->e_entry < span_start || eh->e_entry >= span_end ||
            !vaddr_range_in_load(ph, phnum, eh->e_entry, 1) ||
            eh->e_entry - span_start > ~u64{0} - mapped_addr)
            return false;
        fill_image_tables(ph, phnum, eh->e_phoff,
                          static_cast<u64>(phnum) * sizeof(elf64_phdr), load_bias, out);
        out.entry     = reinterpret_cast<void*>(mapped_addr + (eh->e_entry - span_start));
        out.base      = mapped;
        out.size      = static_cast<usize>(span);
        out.loaded    = (type == et_dyn) ? kind::dyn : kind::executable;
        out.load_bias = load_bias;
        out.nsegs     = seg_i;
        mapping.keep();
        return true;
    }

    core::task<image> load_executable_stream(async_env e, usize file_size) {
        image                result{};
        loader_scratch_owner scratch{new (std::nothrow) loader_scratch{}};

        if (e.map == nullptr || e.unmap == nullptr || e.read == nullptr ||
            file_size < sizeof(elf64_ehdr))
            co_return error_t::einval;
        if (scratch.value == nullptr)
            co_return error_t::enomem;

        auto& ph = scratch.value->ph;

        elf64_ehdr header{};
        auto       header_read = co_await e.read(
            e.ctx, span<u8>(reinterpret_cast<u8*>(&header), sizeof header), 0);
        if (!header_read)
            co_return header_read.error == error_t::enone ? error_t::eio
                                                          : header_read.error;
        if (header_read.value != static_cast<i64>(sizeof header))
            co_return error_t::eio;

        u16 type = 0;
        if (!ident_type(
                span<const u8>(reinterpret_cast<const u8*>(&header), sizeof header),
                type) ||
            header.e_ehsize != sizeof(elf64_ehdr) ||
            (type != et_exec && type != et_dyn) || header.e_phnum == 0 ||
            header.e_phentsize != sizeof(elf64_phdr) ||
            header.e_phnum > max_program_headers ||
            !in_bounds(header.e_phoff,
                       static_cast<u64>(header.e_phnum) * sizeof(elf64_phdr), file_size))
            co_return error_t::einval;

        const usize ph_bytes = static_cast<usize>(header.e_phnum) * sizeof(elf64_phdr);
        auto        ph_read  = co_await e.read(
            e.ctx, span<u8>(reinterpret_cast<u8*>(ph), ph_bytes), header.e_phoff);
        if (!ph_read)
            co_return ph_read.error == error_t::enone ? error_t::eio : ph_read.error;
        if (ph_read.value != static_cast<i64>(ph_bytes))
            co_return error_t::eio;

        u64   min_vaddr     = ~u64{0};
        u64   max_end       = 0;
        u64   map_alignment = page_size;
        usize nsegs         = 0;
        for (u16 i = 0; i < header.e_phnum; ++i) {
            if (ph[i].p_type != pt_load)
                continue;
            if (nsegs >= max_segments ||
                !validate_load_phdr(ph[i], file_size, map_alignment))
                co_return error_t::einval;
            min_vaddr     = ph[i].p_vaddr < min_vaddr ? ph[i].p_vaddr : min_vaddr;
            const u64 end = ph[i].p_vaddr + ph[i].p_memsz;
            max_end       = end > max_end ? end : max_end;
            ++nsegs;
        }
        if (nsegs == 0 || min_vaddr == ~u64{0} || max_end <= min_vaddr ||
            max_end > ~u64{0} - (page_size - 1))
            co_return error_t::einval;

        const u64 span_start = align_down(min_vaddr);
        const u64 span_end   = align_up(max_end);
        if (span_end <= span_start)
            co_return error_t::einval;
        const u64 image_span = span_end - span_start;
        if (image_span > static_cast<u64>(~usize{0}))
            co_return error_t::einval;

        u64  rela_vaddr = 0;
        u64  rela_size  = 0;
        bool rela_tag   = false;
        bool relasz_tag = false;
        for (u16 i = 0; i < header.e_phnum; ++i) {
            if (ph[i].p_type != pt_dynamic)
                continue;
            if (ph[i].p_filesz == 0 || ph[i].p_filesz % sizeof(elf64_dyn) != 0 ||
                !in_bounds(ph[i].p_offset, ph[i].p_filesz, file_size))
                co_return error_t::einval;

            u64  offset     = 0;
            bool terminated = false;
            while (offset < ph[i].p_filesz) {
                const u64   remaining = ph[i].p_filesz - offset;
                const usize length    = remaining < stream_read_chunk_size
                                            ? static_cast<usize>(remaining)
                                            : stream_read_chunk_size;
                auto        read =
                    co_await e.read(e.ctx, span<u8>(scratch.value->buffer, length),
                                    ph[i].p_offset + offset);
                if (!read)
                    co_return read.error == error_t::enone ? error_t::eio : read.error;
                if (read.value != static_cast<i64>(length))
                    co_return error_t::eio;
                const usize count = length / sizeof(elf64_dyn);
                for (usize k = 0; k < count; ++k) {
                    const u8* entry = scratch.value->buffer + k * sizeof(elf64_dyn);
                    const i64 tag   = static_cast<i64>(read_u64_bytes(entry));
                    const u64 value = read_u64_bytes(entry + 8);
                    if (tag == dt_null) {
                        terminated = true;
                        break;
                    }
                    if (tag == dt_rela) {
                        rela_vaddr = value;
                        rela_tag   = true;
                    }
                    else if (tag == dt_relasz) {
                        rela_size  = value;
                        relasz_tag = true;
                    }
                    else if (tag == dt_relaent) {
                        if (value != sizeof(elf64_rela))
                            co_return error_t::einval;
                    }
                }
                offset += length;
                if (terminated)
                    break;
            }
            if (!terminated)
                co_return error_t::einval;
            break;
        }

        if (rela_tag != relasz_tag || rela_size % sizeof(elf64_rela) != 0 ||
            (rela_size != 0 && rela_vaddr == 0) || (rela_size == 0 && rela_vaddr != 0))
            co_return error_t::einval;

        u64 rela_offset = 0;
        if (rela_vaddr != 0 && rela_size != 0) {
            if (!vaddr_range_to_off(ph, header.e_phnum, rela_vaddr, rela_size, file_size,
                                    rela_offset) ||
                !in_bounds(rela_offset, rela_size, file_size))
                co_return error_t::einval;
        }

        const bool fixed = type == et_exec;
        void*      mapped =
            e.map(e.ctx, span_start, static_cast<usize>(image_span),
                  static_cast<usize>(fixed ? page_size : map_alignment), fixed);
        if (mapped == nullptr)
            co_return error_t::enomem;
        async_mapping mapping{e, mapped, static_cast<usize>(image_span)};
        auto          fail = [&mapping](error_t error) {
            mapping.reset();
            return error;
        };
        const u64 mapped_addr = reinterpret_cast<u64>(mapped);
        if ((fixed && mapped_addr != span_start) ||
            !load_bias_alignment_ok(ph, header.e_phnum, mapped_addr, span_start) ||
            image_span > ~u64{0} - mapped_addr)
            co_return fail(error_t::einval);
        const u64 bias = mapped_addr - span_start;

        if (e.copy == nullptr) {
            auto* zero = static_cast<u8*>(mapped);
            for (u64 i = 0; i < image_span; ++i)
                zero[i] = 0;
        }
        else {
            u64 cleared = 0;
            while (cleared < image_span) {
                const u64   remaining = image_span - cleared;
                const usize length    = remaining < stream_read_chunk_size
                                            ? static_cast<usize>(remaining)
                                            : stream_read_chunk_size;
                if (!e.copy(e.ctx, mapped_addr + cleared,
                            span<const u8>(scratch.value->zero, length)))
                    co_return fail(error_t::eio);
                cleared += length;
            }
        }

        usize segment_count = 0;
        for (u16 i = 0; i < header.e_phnum; ++i) {
            if (ph[i].p_type != pt_load)
                continue;
            if (ph[i].p_vaddr < span_start ||
                ph[i].p_vaddr - span_start > image_span - ph[i].p_memsz)
                co_return fail(error_t::einval);
            const u64 destination = mapped_addr + (ph[i].p_vaddr - span_start);
            u64       copied      = 0;
            while (copied < ph[i].p_filesz) {
                const u64   remaining  = ph[i].p_filesz - copied;
                const usize length     = remaining < stream_read_chunk_size
                                             ? static_cast<usize>(remaining)
                                             : stream_read_chunk_size;
                i64         read_value = 0;
                if (e.read_to != nullptr) {
                    auto read = co_await e.read_to(e.ctx, destination + copied, length,
                                                   ph[i].p_offset + copied);
                    if (!read)
                        co_return fail(read.error == error_t::enone ? error_t::eio
                                                                    : read.error);
                    read_value = read.value;
                }
                else {
                    u8* destination_buffer =
                        e.copy != nullptr ? scratch.value->buffer
                                          : reinterpret_cast<u8*>(destination + copied);
                    auto read =
                        co_await e.read(e.ctx, span<u8>(destination_buffer, length),
                                        ph[i].p_offset + copied);
                    if (!read)
                        co_return fail(read.error == error_t::enone ? error_t::eio
                                                                    : read.error);
                    read_value = read.value;
                }
                if (read_value != static_cast<i64>(length))
                    co_return fail(error_t::eio);
                if (e.read_to == nullptr && e.copy != nullptr &&
                    !e.copy(e.ctx, destination + copied,
                            span<const u8>(scratch.value->buffer, length)))
                    co_return fail(error_t::eio);
                copied += length;
            }
            result.segs[segment_count++] = {ph[i].p_vaddr, ph[i].p_filesz, ph[i].p_memsz,
                                            ph[i].p_flags,
                                            reinterpret_cast<void*>(destination)};
        }

        if (rela_vaddr != 0 && rela_size != 0) {
            u64 offset = 0;
            while (offset < rela_size) {
                const u64   remaining = rela_size - offset;
                const usize length    = remaining < stream_rela_chunk_size
                                            ? static_cast<usize>(remaining)
                                            : stream_rela_chunk_size;
                auto        read =
                    co_await e.read(e.ctx, span<u8>(scratch.value->rela_buffer, length),
                                    rela_offset + offset);
                if (!read)
                    co_return fail(read.error == error_t::enone ? error_t::eio
                                                                : read.error);
                if (read.value != static_cast<i64>(length))
                    co_return fail(error_t::eio);
                const usize count = length / sizeof(elf64_rela);
                for (usize k = 0; k < count; ++k) {
                    const u8* entry = scratch.value->rela_buffer + k * sizeof(elf64_rela);
                    const u64 r_info          = read_u64_bytes(entry + 8);
                    const u32 relocation_type = r_type(r_info);
                    if (relocation_type == r_x86_64_none)
                        continue;
                    if (relocation_type != r_x86_64_relative)
                        co_return fail(error_t::einval);
                    const u64 location_vaddr = read_u64_bytes(entry);
                    if (location_vaddr < span_start || location_vaddr > span_end ||
                        sizeof(u64) > span_end - location_vaddr ||
                        !vaddr_range_in_load(ph, header.e_phnum, location_vaddr,
                                             sizeof(u64)) ||
                        location_vaddr - span_start > ~u64{0} - mapped_addr)
                        co_return fail(error_t::einval);
                    const u64 location = mapped_addr + (location_vaddr - span_start);
                    const u64 addend   = read_u64_bytes(entry + 16);
                    u64       value    = 0;
                    if (!checked_biased_value(mapped_addr, span_start,
                                              static_cast<i64>(addend), value))
                        co_return fail(error_t::einval);
                    if (e.copy != nullptr) {
                        u8 value_bytes[sizeof(value)];
                        for (usize byte = 0; byte < sizeof(value); ++byte)
                            value_bytes[byte] = static_cast<u8>(value >> (byte * 8));
                        if (!e.copy(e.ctx, location,
                                    span<const u8>(value_bytes, sizeof value_bytes)))
                            co_return fail(error_t::eio);
                    }
                    else {
                        write_u64(reinterpret_cast<u8*>(location), value);
                    }
                }
                offset += length;
            }
        }

        bool tls_seen = false;
        for (u16 i = 0; i < header.e_phnum; ++i) {
            if (ph[i].p_type != pt_tls)
                continue;
            if (ph[i].p_filesz > ph[i].p_memsz ||
                !in_bounds(ph[i].p_offset, ph[i].p_filesz, file_size) ||
                (ph[i].p_align != 0 && !is_power_of_two(ph[i].p_align)) ||
                ph[i].p_memsz > static_cast<u64>(~usize{0}))
                co_return fail(error_t::einval);
            if (tls_seen)
                co_return fail(error_t::einval);
            tls_seen = true;
            if (ph[i].p_memsz == 0)
                continue;
            const u64 align    = ph[i].p_align == 0 ? 1 : ph[i].p_align;
            u64       tls_span = 0;
            if (!checked_align_up(ph[i].p_memsz, align, tls_span) ||
                tls_span > static_cast<u64>(~usize{0}) - page_size)
                co_return fail(error_t::einval);
            result.tls_offset = ph[i].p_offset;
            result.tls_filesz = ph[i].p_filesz;
            result.tls_memsz  = ph[i].p_memsz;
            result.tls_align  = ph[i].p_align;
        }

        if (header.e_entry < span_start || header.e_entry >= span_end ||
            !vaddr_range_in_load(ph, header.e_phnum, header.e_entry, 1) ||
            header.e_entry - span_start > ~u64{0} - mapped_addr)
            co_return fail(error_t::einval);
        fill_image_tables(ph, header.e_phnum, header.e_phoff, ph_bytes, bias, result);
        result.entry =
            reinterpret_cast<void*>(mapped_addr + (header.e_entry - span_start));
        result.base      = mapped;
        result.size      = static_cast<usize>(image_span);
        result.loaded    = type == et_dyn ? kind::dyn : kind::executable;
        result.load_bias = bias;
        result.nsegs     = segment_count;
        mapping.dismiss();
        co_return result;
    }

} // namespace astia::elf::loader
