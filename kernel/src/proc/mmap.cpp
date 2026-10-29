#include "process.hpp"
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/core/spinlock.hpp>
#include <astia/mm/heap.hpp>

void* operator new(astia::usize size, void* where) noexcept;

namespace astia::proc {

    namespace {
        /// VMA nodes are nullable kernel allocations.  Use the heap's
        /// optional-returning API explicitly rather than relying on the
        /// global non-throwing kernel `operator new` convention; this
        /// makes every failure path below visibly ownership-safe.
        mmap_region* alloc_region() {
            auto storage = mm::heap::alloc(sizeof(mmap_region));
            if (!storage) {
                return nullptr;
            }
            return ::new (storage.value) mmap_region{};
        }

        /// PROT_* word to PTE flags: user and present always; writable
        /// when PROT_WRITE; executable only when PROT_EXEC (x86
        /// defaults to NX).  The arch translates these into its native
        /// bits.
        arch::page_flags pte_flags(map_protection prot) {
            auto flags = arch::page_flags::present | arch::page_flags::user;
            if (has(prot, map_protection::write)) {
                flags = flags | arch::page_flags::writable;
            }
            if (!has(prot, map_protection::exec)) {
                flags = flags | arch::page_flags::no_execute;
            }
            return flags;
        }

        /// Drop the address space's one reference on `victim`'s
        /// backing: a surviving neighbour of the same region (split
        /// siblings are adjacent) inherits it, otherwise the last
        /// mapping of that region is going away.
        void drop_backing(mmap_region* prev, mmap_region* victim) {
            if (!victim->holds_ref) {
                return;
            }
            mmap_region* heir = nullptr;
            if (prev != nullptr && prev->backing == victim->backing) {
                heir = prev;
            }
            else if (victim->next != nullptr &&
                     victim->next->backing == victim->backing) {
                heir = victim->next;
            }
            // An heir that already carries a reference means each
            // surviving sibling owns one — the per-reference siblings a
            // fork clones — so this node's own reference dies with its
            // coverage.  Only a reference-less heir inherits the shared
            // one (siblings split from a single mapping hold one
            // reference between them).
            if (heir != nullptr && !heir->holds_ref) {
                heir->holds_ref = true;
            }
            else {
                victim->backing->release_ref();
            }
            victim->holds_ref = false;
        }

        /// Placement over a base-sorted list: an aligned, free hint
        /// wins (a busy hint is ignored, Linux-style); otherwise the
        /// highest hole that fits below the ceiling — first-fit from
        /// the top keeps MORECORE-style growth monotonic.  0 = no
        /// room.
        u64 place(mmap_region* list, u64 hint, u64 want) {
            const u64 page_bytes = mm::phys_region::frame_size;
            auto      overlaps   = [&](u64 at) -> bool {
                for (mmap_region* it = list; it != nullptr; it = it->next) {
                    if (at < it->end() && it->base < at + want) {
                        return true;
                    }
                }
                return false;
            };
            if (hint != 0 && (hint % page_bytes) == 0 && hint + want >= hint &&
                hint + want <= mmap_region_top && !overlaps(hint)) {
                return hint;
            }
            // Top-down first-fit over every hole, base-ascending: each
            // candidate is the top slice of one hole — [previous end,
            // this base) — and the last hole runs to the placement
            // ceiling.  Keeping the highest slice that fits preserves
            // first-fit-from-the-top, and — unlike a slice taken
            // against a ceiling that later entries were never checked
            // against — can never hand out an address a live mapping
            // already occupies.
            u64 best  = 0;
            u64 lower = 0; // end of the previous VMA (0 before the first)
            for (mmap_region* it = list; it != nullptr; it = it->next) {
                const u64 upper = it->base;
                if (upper >= lower && upper - lower >= want) {
                    best = upper - want; // highest slice of this hole
                }
                lower = it->end();
            }
            if (mmap_region_top >= lower && mmap_region_top - lower >= want) {
                best = mmap_region_top - want;
            }
            return best;
        }
    } // namespace

    mmap_region* process::find_region(u64 addr) {
        core::spinlock_guard guard(mmap_lock_);
        for (mmap_region* it = mmap_regions_; it != nullptr; it = it->next) {
            if (it->covers(addr)) {
                return it;
            }
        }
        return nullptr;
    }

    i32 process::pin_user(u64 addr, usize len, bool destination, mm::iovec& out) {
        if (address_space_ == nullptr || len == 0) {
            return 0; // no address space: kernel memory cannot move
        }
        const u64 page_bytes = mm::phys_region::frame_size;
        const u64 last       = addr + len;
        if (last < addr) {
            return -EINVAL;
        }
        const u64 first = addr & ~(page_bytes - 1);

        core::spinlock_guard guard(mmap_lock_);
        // Resolve the whole range under one lock: a pin names exactly one
        // region, and deriving the VMA per page would let a concurrent
        // munmap from another thread make the range half-resolvable.
        mmap_region* vma = nullptr;
        for (mmap_region* it = mmap_regions_; it != nullptr; it = it->next) {
            if (it->covers(addr)) {
                vma = it;
                break;
            }
        }
        if (vma == nullptr || vma->end() < last) {
            return -EFAULT; // unmapped, or crossing out of the region
        }
        const usize page = static_cast<usize>((first - vma->base) / page_bytes);
        const usize count =
            static_cast<usize>((last - first + page_bytes - 1) / page_bytes);
        // The entry carries the range's in-page offset, so the window the
        // operation is handed names the caller's first byte and not the page
        // it happens to sit on.
        if (!out.append(vma->backing, vma->region_page + page, count,
                        static_cast<usize>(addr - first), len, destination)) {
            return -ENOMEM;
        }
        return 0;
    }

    bool process::install_mapping(u64 base, usize pages, map_protection prot,
                                  mm::phys_region*                region,
                                  const mm::shptr<fs::open_file>& file, u64 file_offset,
                                  usize region_page, bool shared) {
        const u64  page_bytes = mm::phys_region::frame_size;
        const auto flags4     = pte_flags(prot);
        for (u64 page = 0; page < pages; ++page) {
            if (!address_space_->map_4k(
                    base + page * page_bytes,
                    region->base() + (region_page + page) * page_bytes, flags4)) {
                for (u64 done = 0; done < page; ++done) {
                    address_space_->unmap(base + done * page_bytes);
                }
                return false;
            }
        }
        auto* entry = alloc_region();
        if (entry == nullptr) {
            for (u64 page = 0; page < pages; ++page) {
                address_space_->unmap(base + page * page_bytes);
            }
            return false;
        }
        entry->base        = base;
        entry->pages       = pages;
        entry->prot        = prot;
        entry->shared      = shared;
        entry->holds_ref   = true;
        entry->backing     = region;
        entry->region_page = region_page;
        entry->file        = file;
        entry->file_offset = file_offset;
        mapped_bytes_ += pages * page_bytes;
        mmap_region** link = &mmap_regions_;
        while (*link != nullptr && (*link)->base < base) {
            link = &(*link)->next;
        }
        entry->next = *link;
        *link       = entry;
        return true;
    }

    u64 process::map_anonymous(u64 addr, usize len, map_protection prot, map_flags flags) {
        // v1: private anonymous memory; shared mappings come with the
        // page cache and IPC.
        if (!has(flags, map_flags::priv)) {
            return -EINVAL;
        }
        if (!has(flags, map_flags::anonymous)) {
            return -ENOSYS;
        }
        if (has(flags, map_flags::fixed)) {
            return -ENOSYS;
        }
        if (static_cast<u32>(prot) & ~0x7u) {
            return -EINVAL;
        }
        if (address_space_ == nullptr || len == 0) {
            return -EINVAL;
        }
        if (static_cast<u64>(len) > ~u64{0} - 4095u) {
            return -EINVAL;
        }
        const u64 page_bytes = mm::phys_region::frame_size;
        const u64 pages      = (static_cast<u64>(len) + page_bytes - 1) / page_bytes;
        if (pages > (u64{1} << 20)) { // 4 GiB ceiling per call
            return -ENOMEM;
        }
        const u64 want = pages * page_bytes;
        if (want > rlimit_as_.current || mapped_bytes_ > rlimit_as_.current - want)
            return -ENOMEM;

        core::spinlock_guard guard(mmap_lock_);
        const u64            base = place(mmap_regions_, addr, want);
        if (base == 0) {
            return -ENOMEM;
        }
        auto* region = mm::phys_region::create(pages, true);
        if (region == nullptr) {
            return -ENOMEM;
        }
        if (!install_mapping(base, pages, prot, region, {}, 0)) {
            region->release();
            return -ENOMEM;
        }
        return base;
    }

    u64 process::map_file(fd_t fd, u64 addr, usize len, map_protection prot, map_flags flags,
                          u64 off) {
        // File mappings: private mappings capture bytes into
        // fresh frames at map time — snapshot semantics, the same
        // kernel-side read the ELF loader trusts.  Writes land on the
        // private copy; page-cache coherence and MAP_SHARED arrive
        // with the cache and writeback.  MAP_FIXED waits for the
        // merge/replacement rules.
        if ((!has(flags, map_flags::priv) && !has(flags, map_flags::shared)) ||
            (has(flags, map_flags::priv) && has(flags, map_flags::shared))) {
            return -EINVAL;
        }
        if (has(flags, map_flags::anonymous)) {
            return -EINVAL;
        }
        if (has(flags, map_flags::fixed)) {
            return -ENOSYS;
        }
        if (static_cast<u32>(prot) & ~0x7u) {
            return -EINVAL;
        }
        if (address_space_ == nullptr || len == 0) {
            return -EINVAL;
        }
        if (static_cast<u64>(len) > ~u64{0} - 4095u) {
            return -EINVAL;
        }
        const u64 page_bytes = mm::phys_region::frame_size;
        if ((off % page_bytes) != 0) { // POSIX: file offset page-aligned
            return -EINVAL;
        }
        const u64 pages = (static_cast<u64>(len) + page_bytes - 1) / page_bytes;
        if (pages > (u64{1} << 20)) {
            return -ENOMEM;
        }
        const u64 want = pages * page_bytes;
        if (want > rlimit_as_.current || mapped_bytes_ > rlimit_as_.current - want)
            return -ENOMEM;
        if (off + want < off) {
            return -EINVAL;
        }

        auto file = descriptors_.get(fd);
        if (!file) {
            return -EBADF;
        }
        const long file_size = file->size();
        if (file_size < 0) { // devices have no contents to map
            return -ENODEV;
        }

        core::spinlock_guard guard(mmap_lock_);
        const u64            base = place(mmap_regions_, addr, want);
        if (base == 0) {
            return -ENOMEM;
        }
        if (has(flags, map_flags::shared)) {
            auto* object        = file->backing();
            auto* shared_region = object == nullptr ? nullptr : object->memory_region();
            if (shared_region == nullptr || off + want < off ||
                off >= static_cast<u64>(file_size) ||
                off / page_bytes + pages > shared_region->pages()) {
                return -ENOSYS;
            }
            if (!shared_region->share_range(static_cast<usize>(off / page_bytes),
                                            static_cast<usize>(pages))) {
                return -EAGAIN;
            }
            if (!install_mapping(base, pages, prot, shared_region, file, off,
                                 static_cast<usize>(off / page_bytes), true)) {
                shared_region->release_range(static_cast<usize>(off / page_bytes),
                                             static_cast<usize>(pages));
                return -ENOMEM;
            }
            return base;
        }
        // Zeroed frames: the tail past EOF stays zero per POSIX.
        auto* region = mm::phys_region::create(pages, true);
        if (region == nullptr) {
            return -ENOMEM;
        }
        bool io_ok = true;
        for (u64 page = 0; page < pages && io_ok; ++page) {
            const u64 at = off + page * page_bytes;
            if (at >= static_cast<u64>(file_size)) {
                break;
            }
            u64 chunk = page_bytes;
            if (static_cast<u64>(file_size) - at < chunk) {
                chunk = static_cast<u64>(file_size) - at;
            }
            const long got =
                file->read(region->page_direct(page), static_cast<u32>(chunk), at);
            io_ok = got >= 0 && static_cast<u64>(got) == chunk;
        }
        if (!io_ok) {
            region->release();
            return -EIO;
        }
        if (!install_mapping(base, pages, prot, region, file, off)) {
            region->release();
            return -ENOMEM;
        }
        return base;
    }

    i32 process::unmap(u64 addr, usize len) {
        if (address_space_ == nullptr || len == 0 ||
            (addr % mm::phys_region::frame_size) != 0) {
            return -EINVAL;
        }
        const u64 page_bytes = mm::phys_region::frame_size;
        const u64 pages      = (static_cast<u64>(len) + page_bytes - 1) / page_bytes;
        if (pages > (u64{1} << 20) || addr + pages * page_bytes < addr) {
            return -EINVAL;
        }
        const u64 first = addr;
        const u64 last  = addr + pages * page_bytes; // exclusive

        core::spinlock_guard guard(mmap_lock_);

        // A region extending past the range's end is trimmed by giving
        // its tail a new entry; pre-allocate that single node so the
        // unmap cannot fail halfway.
        mmap_region* tail = nullptr;
        for (mmap_region* scan = mmap_regions_; scan != nullptr; scan = scan->next) {
            if (scan->base < last && scan->end() > last) {
                tail = alloc_region();
                if (tail == nullptr) {
                    return -ENOMEM;
                }
                break;
            }
        }

        mmap_region* prev = nullptr;
        mmap_region* it   = mmap_regions_;
        while (it != nullptr) {
            mmap_region* next = it->next;
            if (it->end() <= first || it->base >= last) {
                prev = it; // disjoint: keep walking
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
                address_space_->unmap(virt);
                arch::page_map::invalidate(virt);
            }
            it->backing->unmap_range(
                it->region_page + (drop_from - old_base) / page_bytes,
                static_cast<usize>((drop_to - drop_from) / page_bytes));
            mapped_bytes_ -= drop_to - drop_from;

            if (old_end > last) { // right remainder becomes its own entry
                tail->next        = it->next;
                tail->base        = last;
                tail->pages       = (old_end - last) / page_bytes;
                tail->prot        = it->prot;
                tail->shared      = it->shared;
                tail->holds_ref   = false;
                tail->backing     = it->backing;
                tail->region_page = it->region_page + (last - it->base) / page_bytes;
                tail->file        = it->file; // identity travels with the range
                tail->file_offset = it->file_offset + (last - it->base);
                it->next          = tail;
            }
            if (old_base < first) { // left remainder keeps this entry
                it->pages = (first - old_base) / page_bytes;
                prev      = it;
                it        = it->next;
                continue;
            }
            // Fully covered: unlink and free the entry, dropping the
            // address space's reference on the backing.
            drop_backing(prev, it);
            if (prev == nullptr) {
                mmap_regions_ = it->next;
            }
            else {
                prev->next = it->next;
            }
            delete it; // the shptr releases the open file description
            it = next; // prev stays put
        }
        return 0;
    }

    bool process::register_mapping(u64 base, usize pages, map_protection prot,
                                   mm::phys_region* region) {
        // PTEs are the caller's job — exec segments, the user stack,
        // and the TLS block map their own — and so is adopting the
        // frames; this only files the VMA so fork and CoW see the
        // range.  The entry takes over `region`'s reference.
        if (region == nullptr || pages == 0) {
            return false;
        }
        core::spinlock_guard guard(mmap_lock_);
        auto*                entry = alloc_region();
        if (entry == nullptr) {
            return false;
        }
        entry->base        = base;
        entry->pages       = pages;
        entry->prot        = prot;
        entry->holds_ref   = true;
        entry->backing     = region;
        const u64 bytes = pages * mm::phys_region::frame_size;
        if (bytes > rlimit_as_.current || mapped_bytes_ > rlimit_as_.current - bytes) {
            delete entry;
            return false;
        }
        mapped_bytes_ += bytes;
        mmap_region** link = &mmap_regions_;
        while (*link != nullptr && (*link)->base < base) {
            link = &(*link)->next;
        }
        entry->next = *link;
        *link       = entry;
        return true;
    }

    bool process::clone_address_space_cow(process* child) {
        if (address_space_ == nullptr || child == nullptr ||
            child->address_space_ != nullptr) {
            return false;
        }
        arch::page_map map = arch::page_map::create(arch::kernel_page_map());
        if (!map.valid()) {
            return false;
        }
        const u64 page_bytes = mm::phys_region::frame_size;

        core::spinlock_guard guard(mmap_lock_);
        for (mmap_region* it = mmap_regions_; it != nullptr; it = it->next) {
            if (!it->backing->share_range(it->region_page, it->pages)) {
                return false;
            }
            auto* entry = alloc_region();
            if (entry == nullptr) {
                it->backing->release_range(it->region_page, it->pages);
                return false;
            }
            *entry             = *it;     // shares the open file description
            entry->holds_ref   = true;    // the child's own reference
            entry->next        = nullptr; // never link into the parent's VMA list
            mmap_region** link = &child->mmap_regions_;
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
            const bool cow   = !it->shared && has(it->prot, map_protection::write);
            const auto flags = pte_flags(cow ?
                static_cast<map_protection>(static_cast<u32>(it->prot) &
                                             ~static_cast<u32>(map_protection::write)) : it->prot);
            for (u64 page = 0; page < it->pages; ++page) {
                const u64 virt = it->base + page * page_bytes;
                const u64 phys = address_space_->translate(virt);
                if (phys == arch::page_map::invalid || !map.map_4k(virt, phys, flags)) {
                    return false;
                }
            }
            if (cow) {
                // The parent's copy of the range loses its write bits
                // too (map_4k overwrites the entry and invalidates).
                for (u64 page = 0; page < it->pages; ++page) {
                    const u64 virt = it->base + page * page_bytes;
                    const u64 phys = address_space_->translate(virt);
                    if (phys == arch::page_map::invalid ||
                        !address_space_->map_4k(virt, phys, flags)) {
                        return false;
                    }
                }
            }
        }
        child->address_space_ = new arch::page_map(static_cast<arch::page_map&&>(map));
        child->mapped_bytes_  = mapped_bytes_;
        child->rlimit_nofile_ = rlimit_nofile_;
        child->rlimit_as_     = rlimit_as_;
        child->tls_tp_        = tls_tp_;
        return child->address_space_ != nullptr;
    }

    bool process::cow_fault(u64 addr, bool write) {
        // v1 repairs write faults on private writable mappings only:
        // reads land on present pages, and demand-paging/exec faults
        // stay fatal for now.  The user half runs to 0x800000000000 —
        // mmap_region_top is only the *placement* ceiling; the stack
        // and TLS blocks sit above it and still CoW.
        if (!write || address_space_ == nullptr || addr >= 0x0000800000000000ull) {
            return false;
        }
        const u64 page_bytes = mm::phys_region::frame_size;

        core::spinlock_guard guard(mmap_lock_);
        mmap_region*         vma  = nullptr;
        mmap_region*         prev = nullptr; // heir search needs the left
        for (mmap_region* it = mmap_regions_; it != nullptr; it = it->next) {
            if (it->covers(addr)) {
                vma = it;
                break;
            }
            prev = it;
        }
        if (vma == nullptr || vma->shared || !has(vma->prot, map_protection::write)) {
            return false; // not ours, or a genuine violation
        }
        const bool  held         = vma->holds_ref; // captured before the split
        const u64   virt         = addr & ~(page_bytes - 1);
        const u64   page         = (virt - vma->base) / page_bytes;
        const usize backing_page = vma->region_page + page;
        auto*       back         = vma->backing;
        const auto  flags        = pte_flags(vma->prot);

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
            return address_space_->map_4k(virt, back->base() + backing_page * page_bytes,
                                          flags);
        }

        // Allocate the VMA nodes before changing the PTE.  If either
        // allocation fails, the fault remains unresolved and the
        // original mapping is still valid; never leave a PTE pointing
        // at a copied page with no VMA to own it.
        auto* middle = alloc_region();
        if (middle == nullptr) {
            return false;
        }
        mmap_region* right = nullptr;
        if (page + 1 < vma->pages) {
            right = alloc_region();
            if (right == nullptr) {
                delete middle;
                return false;
            }
        }

        auto* copy = back->split_page(backing_page);
        if (copy == nullptr) {
            delete right;
            delete middle;
            return false; // out of frames: stay fatal
        }
        if (!address_space_->map_4k(virt, copy->base(), flags)) {
            // split_page moved the faulting mapping's coverage to the
            // child.  Restore the parent-page claim before discarding
            // that child if the PTE replacement ever fails.
            back->retain_mapping(backing_page, 1);
            copy->release();
            delete right;
            delete middle;
            return false;
        }

        // Split the VMA around the copied page, Linux-style: the page
        // itself becomes the copy's sole mapping, the flanks keep the
        // shared backing.  Both new nodes are allocated before the
        // list is touched.
        const u64    old_base        = vma->base;
        const usize  old_pages       = vma->pages;
        const usize  old_region_page = vma->region_page;
        const u64    old_offset      = vma->file_offset;
        mmap_region* old_next        = vma->next;

        middle->base        = virt;
        middle->pages       = 1;
        middle->prot        = vma->prot;
        middle->holds_ref   = true; // sole owner of the copy
        middle->backing     = copy;
        middle->region_page = 0;
        middle->file        = vma->file;
        middle->file_offset = old_offset + (virt - old_base);

        if (right != nullptr) {
            right->base        = virt + page_bytes;
            right->pages       = old_pages - page - 1;
            right->prot        = vma->prot;
            right->holds_ref   = false;
            right->backing     = vma->backing;
            right->region_page = old_region_page + page + 1;
            right->file        = vma->file;
            right->file_offset = old_offset + (right->base - old_base);
            right->next        = old_next;
            middle->next       = right;
        }
        else {
            middle->next = old_next;
        }

        if (page > 0) {
            // This entry becomes the left flank, keeping the group's
            // backing reference.
            vma->pages = page;
            vma->next  = middle;
        }
        else {
            // The fault hit the first page: the entry becomes the
            // middle owning the copy and stops covering `back`, so its
            // reference (if this node carries one) needs a new owner.
            vma->base        = middle->base;
            vma->pages       = 1;
            vma->holds_ref   = true; // this node now owns `copy`
            vma->backing     = middle->backing;
            vma->region_page = 0;
            vma->file        = middle->file;
            vma->file_offset = middle->file_offset;
            vma->next        = middle->next;
            delete middle;
            if (held) {
                // A surviving covering sibling that carries no
                // reference inherits this one (siblings split from one
                // mapping share a single reference); when every survivor
                // already holds its own — the per-reference siblings a
                // fork clones — or none survives, the reference dies
                // with this node's coverage of `back`.
                mmap_region* heir = nullptr;
                if (right != nullptr && right->backing == back) {
                    heir = right;
                }
                else if (prev != nullptr && prev->backing == back) {
                    heir = prev;
                }
                else if (old_next != nullptr && old_next->backing == back) {
                    heir = old_next;
                }
                if (heir != nullptr && !heir->holds_ref) {
                    heir->holds_ref = true;
                }
                else {
                    back->release_ref();
                }
            }
        }
        return true;
    }

} // namespace astia::proc
