#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/spinlock_guard.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/mm/heap.hpp>

#include "process.hpp"

void* operator new(astia::usize size, void* where) noexcept;

namespace astia::proc {
    namespace {
        /// VMA nodes are nullable kernel allocations.  Use the heap's
        /// optional-returning API explicitly rather than relying on the
        /// global non-throwing kernel `operator new` convention; this
        /// makes every failure path below visibly ownership-safe.
        mmap_region* alloc_region() {
            auto storage = mm::heap::alloc(sizeof(mmap_region));
            if (!storage)
                return nullptr;

            return ::new (storage.value) mmap_region{};
        }

        /// PROT_* word to PTE flags: user and present always; writable
        /// when PROT_WRITE; executable only when PROT_EXEC (x86
        /// defaults to NX).  The arch translates these into its native
        /// bits.
        mm::mapping_flags pte_flags(map_protection prot) {
            auto flags = mm::mapping_flags::present;

            if (prot != map_protection::none)
                flags = flags | mm::mapping_flags::user;

            if (has(prot, map_protection::write))
                flags = flags | mm::mapping_flags::writable;

            if (!has(prot, map_protection::exec))
                flags = flags | mm::mapping_flags::no_execute;

            return flags;
        }

        /// Drop the address space's one reference on `victim`'s
        /// backing: a surviving neighbour of the same region (split
        /// siblings are adjacent) inherits it, otherwise the last
        /// mapping of that region is going away.
        void drop_backing(mmap_region* prev, mmap_region* victim) {
            if (!victim->holds_ref)
                return;

            mmap_region* heir = nullptr;

            if (prev != nullptr && prev->backing == victim->backing)
                heir = prev;
            else if (victim->next != nullptr && victim->next->backing == victim->backing)
                heir = victim->next;

            // An heir that already carries a reference means each
            // surviving sibling owns one — the per-reference siblings a
            // fork clones — so this node's own reference dies with its
            // coverage.  Only a reference-less heir inherits the shared
            // one (siblings split from a single mapping hold one
            // reference between them).
            if (heir != nullptr && !heir->holds_ref)
                heir->holds_ref = true;
            else
                victim->backing->release_ref();

            victim->holds_ref = false;
        }

        /// Placement over a base-sorted list: an aligned, free hint
        /// wins (a busy hint is ignored, Linux-style); otherwise the
        /// highest hole that fits below the ceiling — first-fit from
        /// the top keeps MORECORE-style growth monotonic.  0 = no
        /// room.
        u64 place(mmap_region* list, u64 hint, u64 want, bool stack = false) {
            const u64 page_bytes = mm::phys_region::frame_size;

            auto overlaps = [&](u64 at) -> bool {
                for (mmap_region* it = list; it != nullptr; it = it->next) {
                    if (at < it->end() && it->base < at + want)
                        return true;
                }
                return false;
            };

            const u64 ceiling = stack ? user_address_limit : user_mmap_top;

            if (hint != 0 && (hint % page_bytes) == 0 && hint + want >= hint &&
                hint + want <= ceiling && !overlaps(hint))
                return hint;

            // Top-down first-fit over every hole, base-ascending: each
            // candidate is the top slice of one hole — [previous end,
            // this base) — and the last hole runs to the placement
            // ceiling.  Keeping the highest slice that fits preserves
            // first-fit-from-the-top, and — unlike a slice taken
            // against a ceiling that later entries were never checked
            // against — can never hand out an address a live mapping
            // already occupies.
            u64 best  = 0;
            u64 lower = 0;

            for (mmap_region* it = list; it != nullptr; it = it->next) {
                const u64 upper = it->base < ceiling ? it->base : ceiling;

                if (upper >= lower && upper - lower >= want)
                    best = upper - want;

                lower = it->end();
            }

            if (user_mmap_top >= lower && user_mmap_top - lower >= want)
                best = user_mmap_top - want;

            return best;
        }

        bool is_zero_device(const fs::open_file& file) {
            const char* path = file.path();

            static constexpr char zero_path[] = "/dev/zero";

            if (path == nullptr)
                return false;

            usize i = 0;

            while (zero_path[i] != '\0') {
                if (path[i] != zero_path[i])
                    return false;

                ++i;
            }

            return path[i] == '\0';
        }
    } // namespace

    mmap_region* process::find_region(u64 addr) {
        arch::interrupt_spinlock_guard guard(_mmap_lock);

        for (mmap_region* it = _mmap_regions; it != nullptr; it = it->next) {
            if (!it->pending && it->covers(addr))
                return it;
        }

        return nullptr;
    }

    i32 process::resolve_shared_futex(u64 addr, shared_futex_mapping& out) {
        out.release();

        const u64 page_bytes = mm::phys_region::frame_size;

        if ((addr & 3u) != 0 || (addr & (page_bytes - 1)) > page_bytes - sizeof(u32))
            return -EINVAL;

        arch::interrupt_spinlock_guard guard(_mmap_lock);
        mmap_region*                   vma = nullptr;

        for (mmap_region* it = _mmap_regions; it != nullptr; it = it->next) {
            if (!it->pending && it->covers(addr)) {
                vma = it;

                break;
            }
        }

        if (vma == nullptr || !vma->shared || vma->backing == nullptr)
            return -EINVAL;

        const usize page =
            vma->region_page + static_cast<usize>((addr - vma->base) / page_bytes);

        if (page >= vma->backing->pages() || !vma->backing->share_range(page, 1))
            return -EFAULT;

        auto  identity = vma->backing->identity();
        auto* direct   = static_cast<u8*>(vma->backing->page_direct(page));

        if (!identity || direct == nullptr) {
            (void) vma->backing->release_range(page, 1);

            return -EFAULT;
        }

        out.backing  = vma->backing;
        out.identity = static_cast<mm::shptr<mm::phys_region_identity>&&>(identity);
        out.offset   = static_cast<usize>(addr & (page_bytes - 1));
        out.page     = page;
        out.word     = reinterpret_cast<u32*>(direct + out.offset);

        return 0;
    }

    i32 process::pin_user(u64 addr, usize len, bool destination, mm::iovec& out) {
        if (!_address_space.attached() || len == 0)
            return 0;

        const u64 page_bytes = mm::phys_region::frame_size;
        const u64 last       = addr + len;
        if (last < addr)
            return -EINVAL;

        auto find = [this](u64 address) -> mmap_region* {
            for (mmap_region* it = _mmap_regions; it != nullptr; it = it->next) {
                if (!it->pending && it->covers(address))
                    return it;
            }
            return nullptr;
        };

        i32 status = 0;

        {
            arch::interrupt_spinlock_guard guard(_mmap_lock);

            // The caller sees one contiguous range, but the address space may
            // split it across adjacent mappings (a DSO's copy-on-write
            // segments are page-granular).  Consecutive pieces that continue
            // the same backing region stay one iovec entry; a real region
            // boundary starts the next.  Pinning is all-or-nothing: on
            // failure the entries this call added are dropped below.
            for (u64 cursor = addr; cursor < last;) {
                mmap_region* vma = find(cursor);
                if (vma == nullptr) {
                    status = -EFAULT;

                    break;
                }

                if (vma->stack && !grow_stack(vma, cursor, destination)) {
                    status = -EFAULT;

                    break;
                }

                const u64   first  = cursor & ~(page_bytes - 1);
                const usize page   = static_cast<usize>((first - vma->base) / page_bytes);
                const usize offset = static_cast<usize>(cursor - first);
                u64         piece_end = vma->end() < last ? vma->end() : last;
                usize       count =
                    static_cast<usize>((piece_end - first + page_bytes - 1) / page_bytes);
                usize length = static_cast<usize>(piece_end - cursor);

                mm::phys_region* region = vma->backing;

                const usize region_page = vma->region_page + page;

                while (piece_end < last) {
                    mmap_region* next = find(piece_end);

                    if (next == nullptr || next->backing != region ||
                        next->region_page != region_page + count)
                        break;

                    if (next->stack && !grow_stack(next, piece_end, destination)) {
                        status = -EFAULT;

                        break;
                    }

                    const u64 next_end = next->end() < last ? next->end() : last;

                    count += static_cast<usize>((next_end - piece_end + page_bytes - 1) /
                                                page_bytes);
                    length += static_cast<usize>(next_end - piece_end);
                    piece_end = next_end;
                }

                if (status < 0)
                    break;

                if (!out.append(region, region_page, count, offset, length,
                                destination)) {
                    status = -ENOMEM;

                    break;
                }

                cursor = piece_end;
            }
        }

        if (status < 0)
            out.release();

        return status;
    }

    bool process::install_mapping(u64 hint, usize pages, map_protection prot,
                                  mm::phys_region*                region,
                                  const mm::shptr<fs::open_file>& file, u64 file_offset,
                                  u64& base_out, usize region_page, bool shared,
                                  bool stack, u64 fixed_base, bool cow) {
        const u64 page_bytes = mm::phys_region::frame_size;

        // A copy-on-write range is writable as a VMA but starts read-only in
        // the page tables, so the first store faults into a private copy.
        const auto pte_prot =
            cow ? static_cast<map_protection>(static_cast<u32>(prot) &
                                              ~static_cast<u32>(map_protection::write))
                : prot;
        const auto flags4 = pte_flags(pte_prot);
        auto*      entry  = alloc_region();
        if (entry == nullptr)
            return false;

        // The pending node is the address-space reservation.  It remains in
        // the sorted list while the expensive page-table work runs unlocked;
        // all readers which could dereference its backing explicitly ignore
        // pending nodes, while placement still treats it as occupied.
        u64  base     = 0;
        bool reserved = false;

        {
            arch::interrupt_spinlock_guard guard(_mmap_lock);

            const u64 want = pages * page_bytes;

            if (fixed_base != 0) {
                base = fixed_base;

                if ((base % page_bytes) != 0 || base + want < base ||
                    base + want > user_address_limit)
                    base = 0;

                for (mmap_region* it = _mmap_regions; base != 0 && it != nullptr;
                     it              = it->next) {
                    if (base < it->end() && it->base < base + want)
                        base = 0;
                }
            }
            else {
                base = place(_mmap_regions, hint, want, stack);
            }

            astia_rlimit address_limit{};

            (void) get_rlimit(RLIMIT_AS, &address_limit);

            if (base != 0 && pages * page_bytes <= address_limit.current &&
                _mapped_bytes <= address_limit.current - pages * page_bytes) {
                entry->base         = base;
                entry->pages        = pages;
                entry->prot         = prot;
                entry->shared       = shared;
                entry->stack        = stack;
                entry->demand_zero  = !stack && region->lazy();
                entry->mapped_pages = stack ? 1 : pages;
                entry->pending      = true;
                entry->holds_ref    = true;
                entry->backing      = region;
                entry->region_page  = region_page;
                entry->file         = file;
                entry->file_offset  = file_offset;
                _mapped_bytes += pages * page_bytes;

                mmap_region** link = &_mmap_regions;

                while (*link != nullptr && (*link)->base < base) {
                    link = &(*link)->next;
                }

                entry->next = *link;
                *link       = entry;
                reserved    = true;
            }
        }

        if (!reserved) {
            delete entry;

            return false;
        }

        const u64 first_page = stack ? pages - 1 : 0;
        const u64 map_pages  = stack ? 1 : pages;

        auto& mapping = _address_space;

        mm::mapping_transaction transaction(mapping, base + first_page * page_bytes,
                                            static_cast<usize>(map_pages));

        for (u64 page = first_page; page < first_page + map_pages; ++page) {
            if (stack && !region->ensure_page(region_page + page, true)) {
                {
                    arch::interrupt_spinlock_guard guard(_mmap_lock);
                    mmap_region**                  link = &_mmap_regions;

                    while (*link != nullptr && *link != entry) {
                        link = &(*link)->next;
                    }

                    if (*link == entry) {
                        *link = entry->next;
                        _mapped_bytes -= pages * page_bytes;
                    }
                }

                delete entry;

                return false;
            }

            // A lazy region may not have this page yet: leave it absent and
            // let the first access materialise it.  Eager regions always
            // have every page, so this changes nothing for them.
            const u64 frame = region->page_base(region_page + page);

            if (frame == mm::frame_allocator::invalid_frame) {
                if (!transaction.skip_4k(base + page * page_bytes)) {
                    transaction.rollback();

                    {
                        arch::interrupt_spinlock_guard guard(_mmap_lock);
                        mmap_region**                  link = &_mmap_regions;

                        while (*link != nullptr && *link != entry) {
                            link = &(*link)->next;
                        }

                        if (*link == entry) {
                            *link = entry->next;
                            _mapped_bytes -= pages * page_bytes;
                        }
                    }

                    delete entry;

                    return false;
                }

                continue;
            }

            if (!transaction.map_4k(
                    base + page * page_bytes, frame,
                    mm::mapping_flags_from_bits(static_cast<u64>(flags4)))) {
                {
                    arch::interrupt_spinlock_guard guard(_mmap_lock);
                    mmap_region**                  link = &_mmap_regions;

                    while (*link != nullptr && *link != entry) {
                        link = &(*link)->next;
                    }

                    if (*link == entry) {
                        *link = entry->next;
                        _mapped_bytes -= pages * page_bytes;
                    }
                }

                // Free after dropping _mmap_lock: heap locking is deliberately
                // not nested under this address-space lock.
                delete entry;

                return false;
            }
        }

        transaction.commit();

        {
            arch::interrupt_spinlock_guard guard(_mmap_lock);

            // No operation removes a pending reservation; retain the check
            // so publication remains an explicit transaction boundary.
            if (entry->pending)
                entry->pending = false;
        }

        base_out = base;

        return true;
    }

    bool process::map_shared_range(u64 base, usize pages, map_protection prot,
                                   mm::phys_region* region, u32 region_page, bool cow) {
        u64 out = 0;

        return install_mapping(base, pages, prot, region, mm::shptr<fs::open_file>{}, 0,
                               out, region_page, false, false, base, cow);
    }

    bool process::grow_stack(mmap_region* vma, u64 addr, bool write) {
        if (vma == nullptr || !vma->stack || vma->pending || !_address_space.attached() ||
            !vma->covers(addr) || (write && !has(vma->prot, map_protection::write)))
            return false;

        const u64 page_bytes = mm::phys_region::frame_size;
        const u64 virt       = addr & ~(page_bytes - 1);
        const u64 frontier   = vma->end() - vma->mapped_pages * page_bytes;

        if (virt >= frontier)
            return _address_space.translate(virt) != mm::address_space::invalid;

        const usize first = static_cast<usize>((virt - vma->base) / page_bytes);
        const usize stop  = vma->pages - vma->mapped_pages;

        const auto flags = pte_flags(vma->prot);

        for (usize page = first; page < stop; ++page) {
            const usize backing_page = vma->region_page + page;

            if (!vma->backing->ensure_page(backing_page, true))
                return false;

            auto page_flags = flags;

            if (!vma->backing->uniquely_held(backing_page))
                page_flags = static_cast<mm::mapping_flags>(
                    static_cast<u64>(page_flags) &
                    ~static_cast<u64>(mm::mapping_flags::writable));

            const u64 mapped = vma->base + page * page_bytes;

            if (_address_space.translate(mapped) == mm::address_space::invalid &&
                !_address_space.map_4k(mapped, vma->backing->page_base(backing_page),
                                       page_flags))
                return false;

            ++vma->mapped_pages;
        }

        return _address_space.translate(virt) != mm::address_space::invalid;
    }

    bool process::materialize_lazy_page(mmap_region* vma, u64 virt, usize backing_page,
                                        bool write) {
        if (vma == nullptr || vma->backing == nullptr || vma->pending ||
            (write && !has(vma->prot, map_protection::write)))
            return false;

        if (!vma->backing->ensure_page(backing_page, true))
            return false;

        // A shared lazy page becomes readable through this mapping; a write
        // still has to split it in cow_fault(), so only a sole owner gets
        // the write bit here.
        const bool unique = vma->backing->uniquely_held(backing_page);

        const auto flags = pte_flags(
            unique
                ? vma->prot
                : static_cast<map_protection>(static_cast<u32>(vma->prot) &
                                              ~static_cast<u32>(map_protection::write)));

        return _address_space.map_4k(virt, vma->backing->page_base(backing_page), flags);
    }

    u64 process::map_anonymous(vaddr_t addr, bytes_t len, map_protection prot,
                               map_flags flags) {
        // v1: private anonymous memory; shared mappings come with the
        // page cache and IPC.
        if (!has(flags, map_flags::priv))
            return -EINVAL;

        if (!has(flags, map_flags::anonymous))
            return -ENOSYS;

        if (has(flags, map_flags::stack) &&
            (!has(flags, map_flags::priv) || !has(flags, map_flags::anonymous)))
            return -EINVAL;

        if (has(flags, map_flags::fixed))
            return -ENOSYS;

        if (!_address_space.attached() || len == 0)
            return -EINVAL;

        if (static_cast<u64>(len) > ~u64{0} - mm::page_mask)
            return -EINVAL;

        const u64 page_bytes = mm::phys_region::frame_size;
        const u64 pages      = (static_cast<u64>(len) + page_bytes - 1) / page_bytes;

        if (pages > (u64{1} << 20)) { // 4 GiB ceiling per call
            return -ENOMEM;
        }

        const u64 want = pages * page_bytes;

        astia_rlimit address_limit{};

        (void) get_rlimit(RLIMIT_AS, &address_limit);

        if (want > address_limit.current || _mapped_bytes > address_limit.current - want)
            return -ENOMEM;

        auto* region = has(flags, map_flags::stack)
                           ? mm::phys_region::create_lazy(pages)
                           : mm::phys_region::create(pages, true);
        if (region == nullptr)
            return -ENOMEM;

        u64        base      = 0;
        const bool installed = install_mapping(addr, pages, prot, region, {}, 0, base, 0,
                                               false, has(flags, map_flags::stack));
        if (!installed) {
            region->release();

            return -ENOMEM;
        }

        return base;
    }

    core::task<u64> process::map_file(fd_t fd, vaddr_t addr, bytes_t len,
                                      map_protection prot, map_flags flags,
                                      offset_t off) {
        // File mappings: private mappings capture bytes into
        // fresh frames at map time — snapshot semantics, the same
        // kernel-side read the ELF loader trusts.  Writes land on the
        // private copy; page-cache coherence and MAP_SHARED arrive
        // with the cache and writeback.  MAP_FIXED waits for the
        // merge/replacement rules.
        if ((!has(flags, map_flags::priv) && !has(flags, map_flags::shared)) ||
            (has(flags, map_flags::priv) && has(flags, map_flags::shared)))
            co_return -EINVAL;

        if (has(flags, map_flags::anonymous))
            co_return -EINVAL;

        if (has(flags, map_flags::stack))
            co_return -EINVAL;

        if (has(flags, map_flags::fixed))
            co_return -ENOSYS;

        if (!_address_space.attached() || len == 0)
            co_return -EINVAL;

        if (static_cast<u64>(len) > ~u64{0} - mm::page_mask)
            co_return -EINVAL;

        const u64 page_bytes = mm::phys_region::frame_size;

        if ((off % page_bytes) != 0) { // POSIX: file offset page-aligned
            co_return -EINVAL;
        }

        const u64 pages = (static_cast<u64>(len) + page_bytes - 1) / page_bytes;

        if (pages > (u64{1} << 20))
            co_return -ENOMEM;

        const u64 want = pages * page_bytes;

        astia_rlimit address_limit{};

        (void) get_rlimit(RLIMIT_AS, &address_limit);

        if (want > address_limit.current || _mapped_bytes > address_limit.current - want)
            co_return -ENOMEM;

        if (off + want < off)
            co_return -EINVAL;

        auto file = _descriptors.get(fd);
        if (!file)
            co_return -EBADF;

        const long file_size = file->size();
        if (file_size < 0) { // devices have no contents to map
            if (!has(flags, map_flags::priv) || !file->mode().is_character_device() ||
                !is_zero_device(*file))
                co_return -ENODEV;

            const u32 access_mode = file->flags() & fs::open_file_access_mask;

            if (has(prot, map_protection::read) &&
                access_mode == static_cast<u32>(fs::open_file_status_flags::write))
                co_return -EACCES;

            auto* region = mm::phys_region::create(pages, true);
            if (region == nullptr)
                co_return -ENOMEM;

            u64 base = 0;

            if (!install_mapping(addr, pages, prot, region, {}, 0, base)) {
                region->release();

                co_return -ENOMEM;
            }

            co_return base;
        }

        const u32 access_mode = file->flags() & fs::open_file_access_mask;

        if (has(prot, map_protection::read) &&
            access_mode == static_cast<u32>(fs::open_file_status_flags::write))
            co_return -EACCES;

        if (has(flags, map_flags::shared) && has(prot, map_protection::write) &&
            access_mode == 0)
            co_return -EACCES;

        if (has(flags, map_flags::shared)) {
            auto* shared_region = file->mapping();

            if (off >= static_cast<u64>(file_size) && prot != map_protection::none)
                co_return -ENXIO;

            if (shared_region == nullptr || off + want < off ||
                off / page_bytes + pages > shared_region->pages())
                co_return -ENOSYS;

            if (!shared_region->share_range(static_cast<usize>(off / page_bytes),
                                            static_cast<usize>(pages)))
                co_return -EAGAIN;

            u64        base = 0;
            const bool installed =
                install_mapping(addr, pages, prot, shared_region, file, off, base,
                                static_cast<usize>(off / page_bytes), true);
            if (!installed) {
                shared_region->release_range(static_cast<usize>(off / page_bytes),
                                             static_cast<usize>(pages));

                co_return -ENOMEM;
            }

            co_return base;
        }

        // Zeroed frames: the tail past EOF stays zero per POSIX.
        auto* region = mm::phys_region::create(pages, true);
        if (region == nullptr)
            co_return -ENOMEM;

        bool io_ok = true;

        for (u64 page = 0; page < pages && io_ok; ++page) {
            const u64 at = off + page * page_bytes;

            if (at >= static_cast<u64>(file_size))
                break;

            u64 chunk = page_bytes;

            if (static_cast<u64>(file_size) - at < chunk)
                chunk = static_cast<u64>(file_size) - at;

            auto got =
                co_await file->read(span<u8>(static_cast<u8*>(region->page_direct(page)),
                                             static_cast<usize>(chunk)),
                                    at, {});

            io_ok = got && got.value >= 0 && static_cast<u64>(got.value) == chunk;
        }

        if (!io_ok) {
            region->release();

            co_return -EIO;
        }

        u64        base = 0;
        const bool installed =
            install_mapping(addr, pages, prot, region, file, off, base);
        if (!installed) {
            region->release();

            co_return -ENOMEM;
        }

        co_return base;
    }

    u64 process::map_file_sync(fd_t fd, vaddr_t addr, bytes_t len, map_protection prot,
                               map_flags flags, offset_t off) {
        auto task = map_file(fd, addr, len, prot, flags, off);

        task.resume();

        if (!task.done())
            return static_cast<u64>(-EAGAIN);

        if (task.failed())
            return static_cast<u64>(error_errno(task.error()));

        return *task.value();
    }

    i32 process::unmap(u64 addr, usize len) {
        if (!_address_space.attached() || len == 0 ||
            (addr % mm::phys_region::frame_size) != 0)
            return -EINVAL;

        const u64 page_bytes = mm::phys_region::frame_size;
        const u64 pages      = (static_cast<u64>(len) + page_bytes - 1) / page_bytes;

        if (pages > (u64{1} << 20) || addr + pages * page_bytes < addr)
            return -EINVAL;

        const u64 first = addr;
        const u64 last  = addr + pages * page_bytes;

        // A region extending past the range's end is trimmed by giving
        // its tail a new entry; pre-allocate that single node so the
        // unmap cannot fail halfway.
        mmap_region* tail = alloc_region();
        if (tail == nullptr)
            return -ENOMEM;

        bool tail_used = false;

        {
            arch::interrupt_spinlock_guard guard(_mmap_lock);
            // A region extending past the range's end needs the preallocated
            // node.  It is harmlessly discarded below when no such region is
            // present.
            mmap_region* prev = nullptr;
            mmap_region* it   = _mmap_regions;

            while (it != nullptr) {
                mmap_region* next = it->next;

                if (it->pending || it->end() <= first || it->base >= last) {
                    prev = it;
                    it   = next;

                    continue;
                }

                const u64 old_base = it->base;
                const u64 old_end  = it->end();

                // The covered middle loses its PTEs; the frames stay with
                // the backing until its last mapping drops.
                const u64 drop_from = old_base < first ? first : old_base;
                const u64 drop_to   = old_end > last ? last : old_end;

                for (u64 virt = drop_from; virt < drop_to; virt += page_bytes) {
                    _address_space.unmap(virt);
                    mm::address_space::invalidate(virt);
                }

                it->backing->unmap_range(
                    it->region_page + (drop_from - old_base) / page_bytes,
                    static_cast<usize>((drop_to - drop_from) / page_bytes));

                _mapped_bytes -= drop_to - drop_from;

                if (old_end > last) {
                    tail_used         = true;
                    tail->next        = it->next;
                    tail->base        = last;
                    tail->pages       = (old_end - last) / page_bytes;
                    tail->prot        = it->prot;
                    tail->shared      = it->shared;
                    tail->stack       = it->stack;
                    tail->demand_zero = it->demand_zero;

                    if (it->stack) {
                        const u64 frontier    = old_end - it->mapped_pages * page_bytes;
                        const u64 mapped_from = frontier > last ? frontier : last;

                        tail->mapped_pages = mapped_from < old_end
                                                 ? (old_end - mapped_from) / page_bytes
                                                 : 0;
                    }
                    else {
                        tail->mapped_pages = tail->pages;
                    }

                    tail->holds_ref   = false;
                    tail->backing     = it->backing;
                    tail->region_page = it->region_page + (last - it->base) / page_bytes;
                    tail->file        = it->file;
                    tail->file_offset = it->file_offset + (last - it->base);
                    it->next          = tail;
                }

                if (old_base < first) {
                    it->pages = (first - old_base) / page_bytes;

                    if (it->stack)
                        it->mapped_pages = 0;
                    else
                        it->mapped_pages = it->pages;

                    prev = it;
                    it   = it->next;

                    continue;
                }

                // Fully covered: unlink and free the entry, dropping the
                // address space's reference on the backing.
                drop_backing(prev, it);

                if (prev == nullptr)
                    _mmap_regions = it->next;
                else
                    prev->next = it->next;

                delete it;

                it = next;
            }
        }

        if (!tail_used)
            delete tail;

        return 0;
    }

    i32 process::protect(u64 addr, usize len, map_protection prot) {
        if (!_address_space.attached() || len == 0 ||
            (addr % mm::phys_region::frame_size) != 0 ||
            static_cast<u64>(len) > ~u64{0} - mm::page_mask)
            return -EINVAL;

        const u64 page_bytes = mm::phys_region::frame_size;
        const u64 pages      = (static_cast<u64>(len) + mm::page_mask) / page_bytes;

        if (pages == 0 || pages > (u64{1} << 20) || pages > (~u64{0} / page_bytes))
            return -EINVAL;

        const u64 first = addr;
        const u64 last  = addr + pages * page_bytes;

        if (last < first || last > user_address_limit)
            return -EINVAL;

        auto segment_mapped_pages = [&](const mmap_region& source, u64 base,
                                        u64 end) -> usize {
            if (!source.stack) {
                return static_cast<usize>((end - base) / page_bytes);
            }
            const u64 frontier = source.end() - source.mapped_pages * page_bytes;
            const u64 mapped   = base > frontier ? base : frontier;
            return mapped < end ? static_cast<usize>((end - mapped) / page_bytes) : 0;
        };
        auto release_nodes = [](mmap_region* nodes) {
            while (nodes != nullptr) {
                mmap_region* next = nodes->next;

                delete nodes;

                nodes = next;
            }
        };

        while (true) {
            usize needed = 0;
            bool  valid  = true;

            {
                arch::interrupt_spinlock_guard guard(_mmap_lock);

                u64 cursor = first;

                mmap_region* it = _mmap_regions;

                while (cursor < last) {
                    while (it != nullptr && it->end() <= cursor) {
                        it = it->next;
                    }

                    if (it == nullptr || it->pending || it->base > cursor) {
                        valid = false;

                        break;
                    }

                    const u64 end = it->end() < last ? it->end() : last;

                    if (cursor < it->base || end <= cursor) {
                        valid = false;

                        break;
                    }

                    if (cursor > it->base)
                        ++needed;

                    if (end < it->end())
                        ++needed;

                    cursor = end;
                    it     = it->next;
                }
            }

            if (!valid)
                return -EINVAL;

            mmap_region* spare = nullptr;

            usize spare_count = 0;

            while (spare_count < needed) {
                mmap_region* node = alloc_region();
                if (node == nullptr) {
                    release_nodes(spare);

                    return -ENOMEM;
                }

                node->next = spare;
                spare      = node;

                ++spare_count;
            }

            bool retry   = false;
            bool success = false;

            {
                arch::interrupt_spinlock_guard guard(_mmap_lock);

                usize current_needed = 0;
                u64   cursor         = first;

                mmap_region* it = _mmap_regions;

                while (cursor < last) {
                    while (it != nullptr && it->end() <= cursor) {
                        it = it->next;
                    }

                    if (it == nullptr || it->pending || it->base > cursor) {
                        retry = false;

                        break;
                    }

                    const u64 end = it->end() < last ? it->end() : last;

                    if (cursor > it->base)
                        ++current_needed;

                    if (end < it->end())
                        ++current_needed;

                    cursor = end;
                    it     = it->next;
                }

                if (cursor != last) {
                    success = false;
                }
                else if (current_needed > spare_count) {
                    retry = true;
                }
                else {
                    cursor = first;
                    it     = _mmap_regions;

                    bool protected_ok = true;

                    while (cursor < last) {
                        while (it != nullptr && it->end() <= cursor) {
                            it = it->next;
                        }

                        const u64   end    = it->end() < last ? it->end() : last;
                        const u64   middle = cursor > it->base ? cursor : it->base;
                        const usize page   = static_cast<usize>(
                            (middle - it->base) / page_bytes + it->region_page);
                        const usize count =
                            static_cast<usize>((end - middle) / page_bytes);

                        for (usize offset = 0; offset < count; ++offset) {
                            const u64 virt = middle + offset * page_bytes;

                            if (_address_space.translate(virt) ==
                                mm::address_space::invalid)
                                continue;

                            auto flags = pte_flags(prot);

                            if (!it->shared && has(prot, map_protection::write) &&
                                !it->backing->uniquely_held(page + offset))
                                flags = static_cast<mm::mapping_flags>(
                                    static_cast<u64>(flags) &
                                    ~static_cast<u64>(mm::mapping_flags::writable));

                            if (!_address_space.protect_4k(virt, flags)) {
                                protected_ok = false;

                                break;
                            }
                        }

                        if (!protected_ok)
                            break;

                        cursor = end;
                        it     = it->next;
                    }

                    if (!protected_ok) {
                        success = false;
                    }
                    else {
                        cursor = first;
                        it     = _mmap_regions;

                        while (cursor < last) {
                            while (it != nullptr && it->end() <= cursor) {
                                it = it->next;
                            }

                            const mmap_region source = *it;

                            const u64 end = source.end() < last ? source.end() : last;
                            const u64 middle =
                                cursor > source.base ? cursor : source.base;
                            const bool left  = middle > source.base;
                            const bool right = end < source.end();

                            mmap_region* middle_node = it;
                            mmap_region* right_node  = nullptr;
                            auto         take_spare  = [&]() {
                                mmap_region* node = spare;

                                spare      = spare->next;
                                node->next = nullptr;

                                --spare_count;

                                return node;
                            };
                            auto fill = [&](mmap_region* node, u64 base, u64 limit,
                                            map_protection value, bool holds_ref) {
                                *node      = source;
                                node->base = base;
                                node->pages =
                                    static_cast<usize>((limit - base) / page_bytes);
                                node->prot = value;
                                node->mapped_pages =
                                    segment_mapped_pages(source, base, limit);
                                node->holds_ref = holds_ref;
                                node->region_page =
                                    source.region_page +
                                    static_cast<usize>((base - source.base) / page_bytes);
                                node->file_offset =
                                    source.file_offset + (base - source.base);
                                node->pending = false;
                                node->next    = nullptr;
                            };

                            if (left) {
                                middle_node = take_spare();
                                right_node  = right ? take_spare() : nullptr;

                                fill(it, source.base, middle, source.prot,
                                     source.holds_ref);
                                fill(middle_node, middle, end, prot, false);

                                if (right_node != nullptr)
                                    fill(right_node, end, source.end(), source.prot,
                                         false);

                                it->next = middle_node;
                                middle_node->next =
                                    right_node != nullptr ? right_node : source.next;

                                if (right_node != nullptr)
                                    right_node->next = source.next;
                            }
                            else {
                                right_node = right ? take_spare() : nullptr;

                                fill(middle_node, middle, end, prot, source.holds_ref);

                                if (right_node != nullptr) {
                                    fill(right_node, end, source.end(), source.prot,
                                         false);

                                    middle_node->next = right_node;
                                    right_node->next  = source.next;
                                }
                                else {
                                    middle_node->next = source.next;
                                }
                            }

                            cursor = end;
                            it     = right_node != nullptr ? right_node->next
                                                           : middle_node->next;
                        }

                        success = true;
                    }
                }
            }

            release_nodes(spare);

            if (retry)
                continue;

            return success ? 0 : -ENOMEM;
        }
    }

    core::task<i32> process::msync(u64 addr, usize len, u32 flags) {
        const u64 page_bytes = mm::phys_region::frame_size;
        const u32 modes      = flags & (MS_SYNC | MS_ASYNC);

        if ((flags & ~(MS_SYNC | MS_ASYNC | MS_INVALIDATE)) != 0 ||
            (modes != MS_SYNC && modes != MS_ASYNC))
            co_return -EINVAL;

        if (len == 0 || (addr % page_bytes) != 0 || len > ~u64{0} - mm::page_mask)
            co_return -EINVAL;

        const u64 pages = (static_cast<u64>(len) + mm::page_mask) / page_bytes;

        if (pages == 0 || pages > (u64{1} << 20) || pages > ~u64{0} / page_bytes)
            co_return -EINVAL;

        const u64 last = addr + pages * page_bytes;

        if (last < addr || last > user_address_limit)
            co_return -EINVAL;

        struct sync_request {
            mm::shptr<fs::open_file> file;
            mm::phys_region*         mapping = nullptr;
            u64                      offset  = 0;
            u64                      length  = 0;
        };

        auto collect = [&](sync_request* requests, usize capacity, usize& count) -> i32 {
            count               = 0;
            u64          cursor = addr;
            mmap_region* it     = _mmap_regions;
            while (cursor < last) {
                while (it != nullptr && it->end() <= cursor) {
                    it = it->next;
                }

                if (it == nullptr || it->pending || it->base > cursor)
                    return -EINVAL;

                const u64 end = it->end() < last ? it->end() : last;
                if (end <= cursor)
                    return -EINVAL;

                if (it->shared) {
                    if (!it->file)
                        return -ENODEV;

                    const u64 delta = cursor - it->base;

                    if (it->file_offset > ~u64{0} - delta)
                        return -EOVERFLOW;

                    if (count == ~usize{0})
                        return -ENOMEM;

                    if (requests != nullptr && count < capacity) {
                        requests[count].file    = it->file;
                        requests[count].mapping = it->backing;
                        requests[count].offset  = it->file_offset + delta;
                        requests[count].length  = end - cursor;
                    }

                    ++count;
                }

                cursor = end;
                it     = it->next;
            }
            return 0;
        };

        struct sync_request* requests = nullptr;

        usize request_count = 0;

        while (true) {
            usize needed = 0;
            i32   result = 0;

            {
                arch::interrupt_spinlock_guard guard(_mmap_lock);

                result = collect(nullptr, 0, needed);
            }

            if (result < 0)
                co_return result;

            if (needed != 0) {
                if (needed > ~usize{0} / sizeof(sync_request))
                    co_return -ENOMEM;

                auto storage = mm::heap::alloc(needed * sizeof(sync_request));
                if (!storage)
                    co_return -ENOMEM;

                requests = reinterpret_cast<sync_request*>(storage.value);

                for (usize i = 0; i < needed; ++i) {
                    ::new (requests + i) sync_request{};
                }
            }

            usize actual = 0;

            {
                arch::interrupt_spinlock_guard guard(_mmap_lock);

                result = collect(requests, needed, actual);
            }

            if (result < 0) {
                for (usize i = 0; i < needed; ++i) {
                    requests[i].~sync_request();
                }

                if (requests != nullptr)
                    mm::heap::free(requests);

                co_return result;
            }

            if (actual == needed) {
                request_count = needed;

                break;
            }

            for (usize i = 0; i < needed; ++i) {
                requests[i].~sync_request();
            }

            if (requests != nullptr)
                mm::heap::free(requests);

            requests = nullptr;
        }

        i32 result = 0;

        for (usize i = 0; i < request_count; ++i) {
            auto contract = requests[i].file->parent()->as_mappable();
            if (!contract) {
                result = -ENODEV;

                break;
            }

            auto synced = co_await contract->sync_mapping(
                requests[i].mapping, requests[i].offset, requests[i].length, flags, {});
            if (!synced) {
                result = static_cast<i32>(error_errno(synced.error));

                break;
            }

            if (synced.value < 0) {
                result = static_cast<i32>(synced.value);

                break;
            }
        }

        for (usize i = 0; i < request_count; ++i) {
            requests[i].~sync_request();
        }

        if (requests != nullptr)
            mm::heap::free(requests);

        co_return result;
    }

    i32 process::msync_sync(u64 addr, usize len, u32 flags) {
        auto task = msync(addr, len, flags);

        task.resume();

        if (!task.done())
            return -EAGAIN;

        if (task.failed())
            return static_cast<i32>(error_errno(task.error()));

        return *task.value();
    }

    bool process::register_mapping(u64 base, usize pages, map_protection prot,
                                   mm::phys_region* region) {
        // PTEs are the caller's job — exec segments, the user stack,
        // and the TLS block map their own — and so is adopting the
        // frames; this only files the VMA so fork and CoW see the
        // range.  The entry takes over `region`'s reference.
        if (region == nullptr || pages == 0)
            return false;

        auto* entry = alloc_region();
        if (entry == nullptr)
            return false;

        entry->base         = base;
        entry->pages        = pages;
        entry->mapped_pages = pages;
        entry->prot         = prot;
        entry->demand_zero  = region->lazy();
        entry->holds_ref    = true;
        entry->backing      = region;

        const u64 bytes  = pages * mm::phys_region::frame_size;
        bool      linked = false;

        {
            arch::interrupt_spinlock_guard guard(_mmap_lock);
            astia_rlimit                   address_limit{};

            (void) get_rlimit(RLIMIT_AS, &address_limit);

            if (bytes <= address_limit.current &&
                _mapped_bytes <= address_limit.current - bytes) {
                _mapped_bytes += bytes;

                mmap_region** link = &_mmap_regions;

                while (*link != nullptr && (*link)->base < base) {
                    link = &(*link)->next;
                }

                entry->next = *link;
                *link       = entry;
                linked      = true;
            }
        }

        if (!linked)
            delete entry;

        return linked;
    }

    bool process::clone_address_space_cow(process* child) {
        if (!_address_space.attached() || child == nullptr ||
            child->_address_space.attached())
            return false;

        mm::address_space map = mm::address_space::create();

        if (!map.valid())
            return false;

        const u64 page_bytes = mm::phys_region::frame_size;

        arch::interrupt_spinlock_guard guard(_mmap_lock);

        // Keep the original parent write state for every private writable
        // page.  A page may already be read-only because an earlier fork
        // owns a sharing reference; such a page must not be made writable by
        // rolling this fork back.
        usize cow_pages = 0;

        for (mmap_region* it = _mmap_regions; it != nullptr; it = it->next) {
            if (it->pending)
                return false;

            if (!it->shared && has(it->prot, map_protection::write)) {
                if (cow_pages > ~usize{0} - it->pages)
                    return false;

                cow_pages += it->pages;
            }
        }

        auto saved_writable_storage = mm::heap::alloc(cow_pages);

        if (cow_pages != 0 && !saved_writable_storage)
            return false;

        auto* saved_writable =
            cow_pages == 0 ? nullptr : static_cast<u8*>(saved_writable_storage.value);

        usize saved_writable_count = 0;

        auto rollback = [&]() {
            // Restore only bits this transaction actually changed.  This is
            // important when the parent was already a CoW participant.
            usize state_at = 0;

            for (mmap_region* it = _mmap_regions; it != nullptr; it = it->next) {
                const bool cow = !it->shared && has(it->prot, map_protection::write);

                for (u64 page = 0; page < it->pages; ++page) {
                    const u64 virt = it->base + page * page_bytes;

                    if (cow && state_at < saved_writable_count &&
                        saved_writable[state_at] != 0) {
                        const u64 phys = _address_space.translate(virt);

                        if (phys != mm::address_space::invalid)
                            (void) _address_space.map_4k(virt, phys, pte_flags(it->prot));
                    }

                    if (cow)
                        ++state_at;
                }
            }

            while (child->_mmap_regions != nullptr) {
                mmap_region* victim = child->_mmap_regions;

                child->_mmap_regions = victim->next;

                for (usize page = 0; page < victim->pages; ++page) {
                    child->_address_space.attached()
                        ? child->_address_space.unmap(victim->base + page * page_bytes)
                        : map.unmap(victim->base + page * page_bytes);
                }

                victim->backing->unmap_range(victim->region_page, victim->pages);

                if (victim->holds_ref)
                    victim->backing->release_ref();

                delete victim;
            }

            if (saved_writable_storage)
                mm::heap::free(saved_writable_storage.value);
        };

        for (mmap_region* it = _mmap_regions; it != nullptr; it = it->next) {
            if (!it->backing->share_range(it->region_page, it->pages)) {
                rollback();

                return false;
            }

            auto* entry = alloc_region();
            if (entry == nullptr) {
                rollback();

                return false;
            }

            *entry           = *it;
            entry->holds_ref = true;
            entry->next      = nullptr;

            mmap_region** link = &child->_mmap_regions;

            while (*link != nullptr) {
                link = &(*link)->next;
            }

            *link = entry;

            // Private writable pages turn copy-on-write in BOTH
            // spaces: the child maps them read-only and the parent's
            // write bits come off, so the first write on either side
            // faults into a private copy.  Read-only ranges (text,
            // the TLS template) keep their flags and are shared as
            // they are.
            const bool cow = !it->shared && has(it->prot, map_protection::write);

            const auto flags =
                pte_flags(cow ? static_cast<map_protection>(
                                    static_cast<u32>(it->prot) &
                                    ~static_cast<u32>(map_protection::write))
                              : it->prot);

            for (u64 page = 0; page < it->pages; ++page) {
                const u64 virt = it->base + page * page_bytes;
                const u64 phys = _address_space.translate(virt);

                if (phys == mm::address_space::invalid)
                    continue;

                if (!map.map_4k(virt, phys, flags)) {
                    for (u64 done = 0; done < page; ++done) {
                        (void) map.unmap(it->base + done * page_bytes);
                    }

                    rollback();

                    return false;
                }
            }

            if (cow) {
                // map_4k overwrites the entry and invalidates it.
                for (u64 page = 0; page < it->pages; ++page) {
                    const u64 virt = it->base + page * page_bytes;

                    saved_writable[saved_writable_count++] =
                        _address_space.writable(virt) ? 1 : 0;

                    if (saved_writable[saved_writable_count - 1] == 0)
                        continue;

                    const u64 phys = _address_space.translate(virt);

                    if (phys == mm::address_space::invalid ||
                        !_address_space.map_4k(virt, phys, flags)) {
                        rollback();

                        return false;
                    }
                }
            }
        }

        child->_address_space = static_cast<mm::address_space&&>(map);

        if (!child->_address_space.attached()) {
            rollback();

            return false;
        }

        child->_mapped_bytes = _mapped_bytes;

        astia_rlimit nofile_limit{}, address_limit{};

        (void) get_rlimit(RLIMIT_NOFILE, &nofile_limit);
        (void) get_rlimit(RLIMIT_AS, &address_limit);
        (void) child->set_rlimit(RLIMIT_NOFILE, nofile_limit);
        (void) child->set_rlimit(RLIMIT_AS, address_limit);

        child->_tls_tp     = _tls_tp;
        child->_exec_image = _exec_image;

        if (saved_writable_storage)
            mm::heap::free(saved_writable_storage.value);

        return true;
    }

    bool process::cow_fault(u64 addr, bool write) {
        // v1 repairs write faults on private writable mappings only:
        // reads land on present pages, and demand-paging/exec faults
        // stay fatal for now.  The user half runs to 0x800000000000 —
        // user_mmap_top is only the *placement* ceiling; the stack
        // and TLS blocks sit above it and still CoW.
        if (!_address_space.attached() || addr >= user_address_limit)
            return false;

        const u64 page_bytes = mm::phys_region::frame_size;

        arch::interrupt_spinlock_guard guard(_mmap_lock);
        mmap_region*                   vma  = nullptr;
        mmap_region*                   prev = nullptr; // heir search needs the left

        for (mmap_region* it = _mmap_regions; it != nullptr; it = it->next) {
            if (!it->pending && it->covers(addr)) {
                vma = it;

                break;
            }

            prev = it;
        }

        if (vma == nullptr) {
            return false; // not ours, or a genuine violation
        }

        const bool  held         = vma->holds_ref; // captured before the split
        const u64   virt         = addr & ~(page_bytes - 1);
        const u64   page         = (virt - vma->base) / page_bytes;
        const usize backing_page = vma->region_page + page;

        auto* back = vma->backing;

        if (vma->stack && _address_space.translate(virt) == mm::address_space::invalid)
            return grow_stack(vma, addr, write);

        if (vma->demand_zero &&
            _address_space.translate(virt) == mm::address_space::invalid) {
            if (!materialize_lazy_page(vma, virt, backing_page, write))
                return false;

            // A sole owner is writable now; a shared page stays read-only
            // and falls through to the ordinary CoW split below.
            if (!write || back->uniquely_held(backing_page))
                return true;
        }

        if (!write || vma->shared || !has(vma->prot, map_protection::write)) {
            return false; // not ours, or a genuine violation
        }

        const auto flags = pte_flags(vma->prot);

        if (back->destination_pinned(backing_page)) {
            // A device is filling this frame for a read the caller is
            // waiting on.  Splitting now would hand this faulting
            // instruction a fresh copy while the device keeps writing the
            // frame the operation pinned, so the read's bytes would land
            // where the caller can never see them.  Decline: writing into
            // a buffer an in-flight read is filling is a userspace race,
            // and a loud fault beats a silently lost read.
            return false;
        }

        if (back->uniquely_held(backing_page)) {
            // Sole mapper left among the sharers: the write bit comes
            // straight back, no copy.
            return _address_space.map_4k(virt, back->page_base(backing_page), flags);
        }

        // Allocate the VMA nodes before changing the PTE.  If either
        // allocation fails, the fault remains unresolved and the
        // original mapping is still valid; never leave a PTE pointing
        // at a copied page with no VMA to own it.
        auto* middle = alloc_region();
        if (middle == nullptr)
            return false;

        mmap_region* right = nullptr;

        if (page + 1 < vma->pages) {
            right = alloc_region();
            if (right == nullptr) {
                delete middle;

                return false;
            }
        }

        auto* copy = back->copy_page(backing_page);
        if (copy == nullptr) {
            delete right;
            delete middle;

            return false; // out of frames: stay fatal
        }

        if (!_address_space.map_4k(virt, copy->base(), flags)) {
            copy->release();
            delete right;
            delete middle;

            return false;
        }

        if (!back->unmap_range(backing_page, 1)) {
            (void) _address_space.map_4k(virt, back->page_base(backing_page), flags);
            copy->release();
            delete right;
            delete middle;

            return false;
        }

        // Split the VMA around the copied page, Linux-style: the page
        // itself becomes the copy's sole mapping, the flanks keep the
        // shared backing.  Both new nodes are allocated before the
        // list is touched.
        const u64   old_base        = vma->base;
        const usize old_pages       = vma->pages;
        const usize old_mapped      = vma->mapped_pages;
        const usize old_frontier    = old_pages - old_mapped;
        const usize old_region_page = vma->region_page;
        const u64   old_offset      = vma->file_offset;

        mmap_region* old_next = vma->next;

        middle->base         = virt;
        middle->pages        = 1;
        middle->prot         = vma->prot;
        middle->stack        = vma->stack;
        middle->demand_zero  = vma->demand_zero;
        middle->mapped_pages = 1;
        middle->holds_ref    = true; // sole owner of the copy
        middle->backing      = copy;
        middle->region_page  = 0;
        middle->file         = vma->file;
        middle->file_offset  = old_offset + (virt - old_base);

        if (right != nullptr) {
            right->base         = virt + page_bytes;
            right->pages        = old_pages - page - 1;
            right->prot         = vma->prot;
            right->stack        = vma->stack;
            right->demand_zero  = vma->demand_zero;
            right->mapped_pages = old_pages - page - 1;
            right->holds_ref    = false;
            right->backing      = vma->backing;
            right->region_page  = old_region_page + page + 1;
            right->file         = vma->file;
            right->file_offset  = old_offset + (right->base - old_base);
            right->next         = old_next;
            middle->next        = right;
        }
        else {
            middle->next = old_next;
        }

        if (page > 0) {
            // This entry becomes the left flank, keeping the group's
            // backing reference.
            vma->pages        = page;
            vma->mapped_pages = page > old_frontier ? page - old_frontier : 0;
            vma->next         = middle;
        }
        else {
            // The fault hit the first page: the entry becomes the
            // middle owning the copy and stops covering `back`, so its
            // reference (if this node carries one) needs a new owner.
            vma->base         = middle->base;
            vma->pages        = 1;
            vma->stack        = middle->stack;
            vma->mapped_pages = 1;
            vma->holds_ref    = true; // this node now owns `copy`
            vma->backing      = middle->backing;
            vma->region_page  = 0;
            vma->file         = middle->file;
            vma->file_offset  = middle->file_offset;
            vma->next         = middle->next;

            delete middle;

            if (held) {
                // A surviving covering sibling that carries no
                // reference inherits this one (siblings split from one
                // mapping share a single reference); when every survivor
                // already holds its own — the per-reference siblings a
                // fork clones — or none survives, the reference dies
                // with this node's coverage of `back`.
                mmap_region* heir = nullptr;

                if (right != nullptr && right->backing == back)
                    heir = right;
                else if (prev != nullptr && prev->backing == back)
                    heir = prev;
                else if (old_next != nullptr && old_next->backing == back)
                    heir = old_next;

                if (heir != nullptr && !heir->holds_ref)
                    heir->holds_ref = true;
                else
                    back->release_ref();
            }
        }

        return true;
    }
} // namespace astia::proc
