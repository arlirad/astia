#include <astia/mm/heap.hpp>
#include <astia/mm/phys_region.hpp>

namespace astia::mm {

    namespace {
        constexpr u16 max_page_mappings = ~u16{0};
    } // namespace

    bool phys_region::initialize(u64 base, usize pages, bool lazy) {
        if (pages > (~usize{0}) / sizeof(u16)) {
            return false;
        }
        shptr<phys_region_identity> identity(new phys_region_identity{});
        if (!identity) {
            return false;
        }
        auto storage = heap::alloc(pages * sizeof(u16));
        if (!storage) {
            return false;
        }
        auto destinations = heap::alloc(pages * sizeof(u16));
        if (!destinations) {
            heap::free(storage.value);
            return false;
        }
        void* page_bases = nullptr;
        if (lazy) {
            if (pages > (~usize{0}) / sizeof(u64)) {
                heap::free(destinations.value);
                heap::free(storage.value);
                return false;
            }
            auto bases = heap::alloc(pages * sizeof(u64));
            if (!bases) {
                heap::free(destinations.value);
                heap::free(storage.value);
                return false;
            }
            page_bases = bases.value;
        }
        base_      = base;
        pages_     = pages;
        identity_  = static_cast<shptr<phys_region_identity>&&>(identity);
        page_refs_ = static_cast<u16*>(static_cast<void*>(storage.value));
        dest_refs_ = static_cast<u16*>(static_cast<void*>(destinations.value));
        page_bases_ = static_cast<u64*>(page_bases);
        // A freshly-created/adopted region carries its initial
        // mapping reference.  This preserves the existing ownership
        // contract: the creator transfers that reference to its VMA.
        for (usize i = 0; i < pages; ++i) {
            page_refs_[i] = 1;
            dest_refs_[i] = 0;
            if (page_bases_ != nullptr)
                page_bases_[i] = frame_allocator::invalid_frame;
        }
        return true;
    }

    phys_region* phys_region::create(usize page_count, bool zero) {
        if (page_count == 0) {
            return nullptr;
        }
        const u64 base = kernel_frames().alloc(page_count, zero ? frame_flags::zeroed
                                                                : frame_flags::none);
        if (base == frame_allocator::invalid_frame) {
            return nullptr;
        }
        auto* region = new phys_region{};
        if (region == nullptr || !region->initialize(base, page_count)) {
            if (region != nullptr) {
                delete region;
            }
            kernel_frames().release(base, page_count);
            return nullptr;
        }
        return region;
    }

    phys_region* phys_region::create_lazy(usize page_count) {
        if (page_count == 0) {
            return nullptr;
        }
        auto* region = new phys_region{};
        if (region == nullptr || !region->initialize(0, page_count, true)) {
            delete region;
            return nullptr;
        }
        return region;
    }

    phys_region* phys_region::adopt(u64 base, usize page_count) {
        if (base == 0 || (base % frame_size) != 0 || page_count == 0) {
            return nullptr;
        }
        auto* region = new phys_region{};
        if (region == nullptr || !region->initialize(base, page_count)) {
            delete region;
            return nullptr;
        }
        return region;
    }

    bool phys_region::retain_mapping(usize first, usize count) {
        if (count == 0) {
            return true;
        }
        if (first >= pages_ || count > pages_ - first || page_refs_ == nullptr) {
            return false;
        }
        usize changed = 0;
        for (; changed < count; ++changed) {
            u16* slot = &page_refs_[first + changed];
            u16  old  = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
            for (;;) {
                if (old == 0 || old == max_page_mappings) {
                    for (usize rollback = 0; rollback < changed; ++rollback) {
                        u16* prior = &page_refs_[first + rollback];
                        __atomic_fetch_sub(prior, u16{1}, __ATOMIC_ACQ_REL);
                    }
                    return false;
                }
                const u16 next = static_cast<u16>(old + 1);
                if (__atomic_compare_exchange_n(slot, &old, next, false, __ATOMIC_ACQ_REL,
                                                __ATOMIC_ACQUIRE)) {
                    break;
                }
            }
        }
        return true;
    }

    bool phys_region::unmap_range(usize first, usize count) {
        if (count == 0) {
            return true;
        }
        if (first >= pages_ || count > pages_ - first || page_refs_ == nullptr) {
            return false;
        }
        for (usize i = 0; i < count; ++i) {
            u16* slot = &page_refs_[first + i];
            u16  old  = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
            for (;;) {
                // A page may already be zero when a caller is releasing
                // an old full-range reference after CoW moved its page
                // into a one-page child.  That is an idempotent release.
                if (old == 0) {
                    break;
                }
                const u16 next = static_cast<u16>(old - 1);
                if (__atomic_compare_exchange_n(slot, &old, next, false, __ATOMIC_ACQ_REL,
                                                __ATOMIC_ACQUIRE)) {
                    if (old == 1) {
                        const u64 frame = page_base(first + i);
                        if (frame != frame_allocator::invalid_frame) {
                            if (page_bases_ != nullptr)
                                __atomic_store_n(&page_bases_[first + i],
                                                 frame_allocator::invalid_frame,
                                                 __ATOMIC_RELEASE);
                            kernel_frames().release(frame);
                        }
                    }
                    break;
                }
            }
        }
        return true;
    }

    bool phys_region::share_range(usize first, usize count) {
        if (count == 0 || first >= pages_ || count > pages_ - first) {
            return false;
        }
        {
            core::spinlock_guard guard(lock_);
            if (refs_ == ~u32{0}) {
                return false;
            }
            ++refs_;
        }
        if (!retain_mapping(first, count)) {
            core::spinlock_guard guard(lock_);
            --refs_;
            return false;
        }
        return true;
    }

    bool phys_region::addref() {
        // Legacy full-range sharing API.  New VMA code uses share_range()
        // so partial file mappings do not charge unrelated pages.
        return share_range(0, pages_);
    }

    u32 phys_region::references() {
        core::spinlock_guard guard(lock_);
        return refs_;
    }

    void* phys_region::page_direct(usize index) {
        const u64 frame = page_base(index);
        return frame == frame_allocator::invalid_frame ? nullptr : kernel_frames().to_virt(frame);
    }

    u64 phys_region::page_base(usize index) const {
        if (index >= pages_) {
            return frame_allocator::invalid_frame;
        }
        if (page_bases_ != nullptr) {
            return __atomic_load_n(&page_bases_[index], __ATOMIC_ACQUIRE);
        }
        return base_ + index * frame_size;
    }

    bool phys_region::ensure_page(usize index, bool zero, bool* created) {
        if (created != nullptr)
            *created = false;
        if (index >= pages_) {
            return false;
        }
        if (page_bases_ == nullptr) {
            return page_base(index) != frame_allocator::invalid_frame;
        }
        core::spinlock_guard guard(lock_);
        if (page_bases_[index] != frame_allocator::invalid_frame) {
            return true;
        }
        const u64 frame = kernel_frames().alloc(1, zero ? frame_flags::zeroed
                                                        : frame_flags::none);
        if (frame == frame_allocator::invalid_frame) {
            return false;
        }
        page_bases_[index] = frame;
        if (created != nullptr)
            *created = true;
        return true;
    }

    void phys_region::release_ref() {
        child*       orphans = nullptr;
        phys_region* parent  = nullptr;
        {
            core::spinlock_guard guard(lock_);
            if (refs_ == 0) {
                return;
            }
            if (refs_ > 1) {
                --refs_;
                return;
            }
            refs_     = 0;
            orphans   = children_;
            children_ = nullptr;
            parent    = parent_;
            parent_   = nullptr;
        }

        while (orphans != nullptr) {
            child* node = orphans;
            orphans     = node->next;
            node->region->detach_parent(this);
            heap::free(node);
        }
        if (parent != nullptr) {
            parent->unlink_child(this);
        }

        // Any still-live pages are owned by this last allocation
        // reference.  Normally their mapping counters are already zero;
        // releasing here is the defensive final cleanup.
        for (usize i = 0; i < pages_; ++i) {
            u16 old = __atomic_exchange_n(&page_refs_[i], u16{0}, __ATOMIC_ACQ_REL);
            if (old != 0) {
                const u64 frame = page_base(i);
                if (frame != frame_allocator::invalid_frame) {
                    if (page_bases_ != nullptr)
                        __atomic_store_n(&page_bases_[i], frame_allocator::invalid_frame,
                                         __ATOMIC_RELEASE);
                    kernel_frames().release(frame);
                }
            }
        }
        heap::free(dest_refs_);
        dest_refs_ = nullptr;
        heap::free(page_bases_);
        page_bases_ = nullptr;
        heap::free(page_refs_);
        identity_ = {};
        delete this;
    }

    bool phys_region::release_range(usize first, usize count) {
        if (!unmap_range(first, count)) {
            return false;
        }
        release_ref();
        return true;
    }

    void phys_region::release() {
        // Legacy full-range release.  VMA teardown uses unmap_range for
        // every surviving sibling and then release_ref() once.
        (void) unmap_range(0, pages_);
        release_ref();
    }

    void phys_region::unlink_child(phys_region* target) {
        child* dead = nullptr;
        {
            core::spinlock_guard guard(lock_);
            child**              link = &children_;
            while (*link != nullptr) {
                if ((*link)->region == target) {
                    dead  = *link;
                    *link = dead->next;
                    break;
                }
                link = &(*link)->next;
            }
        }

        // Heap operations are external to the region's lock.  In particular,
        // keeping this lock while taking heap::lock_ would invert the order
        // against callers that already own heap metadata and then touch a
        // region.
        if (dead != nullptr)
            heap::free(dead);
    }

    void phys_region::detach_parent(const phys_region* expected) {
        core::spinlock_guard guard(lock_);
        if (parent_ == expected) {
            parent_ = nullptr;
        }
    }

    phys_region* phys_region::split_page(usize index) {
        auto* copy = copy_page(index);
        if (copy == nullptr || !unmap_range(index, 1)) {
            if (copy != nullptr)
                copy->release();
            return nullptr;
        }
        return copy;
    }

    phys_region* phys_region::copy_page(usize index) {
        // Keep a temporary atomic page claim while allocation and copying run
        // without lock_.  A concurrent unmap can consume its own claim, but
        // cannot free the source frame until this claim is dropped.
        if (index >= pages_ || page_refs_ == nullptr || !retain_mapping(index, 1)) {
            return nullptr;
        }
        auto* copy = create(1, false); // the bytes come over below
        if (copy == nullptr) {
            (void) unmap_range(index, 1);
            return nullptr;
        }
        auto node = heap::alloc(sizeof(child));
        if (!node) {
            (void) unmap_range(index, 1);
            copy->release();
            return nullptr;
        }

        const auto* source = static_cast<const u8*>(page_direct(index));
        auto*       target = static_cast<u8*>(copy->page_direct(0));
        if (source == nullptr || target == nullptr) {
            (void) unmap_range(index, 1);
            heap::free(node.value);
            copy->release();
            return nullptr;
        }
        for (usize b = 0; b < frame_size; ++b) {
            target[b] = source[b];
        }

        (void) unmap_range(index, 1);

        auto* list_node = static_cast<child*>(static_cast<void*>(node.value));
        {
            core::spinlock_guard guard(lock_);
            list_node->region      = copy;
            list_node->parent_page = index;
            list_node->next        = children_;
            children_              = list_node;
            copy->parent_          = this;
            copy->parent_page_     = index;
        }
        return copy;
    }

    bool phys_region::uniquely_held(usize index) {
        if (index >= pages_ || page_refs_ == nullptr) {
            return false;
        }
        return __atomic_load_n(&page_refs_[index], __ATOMIC_ACQUIRE) == 1;
    }

    bool phys_region::retain_destination(usize first, usize count) {
        if (count == 0) {
            return true;
        }
        if (first >= pages_ || count > pages_ - first || dest_refs_ == nullptr) {
            return false;
        }
        usize marked = 0;
        for (; marked < count; ++marked) {
            u16* slot = &dest_refs_[first + marked];
            u16  old  = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
            for (;;) {
                if (old == max_page_mappings) {
                    release_destination(first, marked);
                    return false;
                }
                const u16 next = static_cast<u16>(old + 1);
                if (__atomic_compare_exchange_n(slot, &old, next, false, __ATOMIC_ACQ_REL,
                                                __ATOMIC_ACQUIRE)) {
                    break;
                }
            }
        }
        return true;
    }

    void phys_region::release_destination(usize first, usize count) {
        if (count == 0 || first >= pages_ || count > pages_ - first ||
            dest_refs_ == nullptr) {
            return;
        }
        for (usize i = 0; i < count; ++i) {
            u16* slot = &dest_refs_[first + i];
            u16  old  = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
            for (;;) {
                if (old == 0) {
                    break; // an idempotent release, as unmap_range() allows
                }
                const u16 next = static_cast<u16>(old - 1);
                if (__atomic_compare_exchange_n(slot, &old, next, false, __ATOMIC_ACQ_REL,
                                                __ATOMIC_ACQUIRE)) {
                    break;
                }
            }
        }
    }

    bool phys_region::destination_pinned(usize index) const {
        if (index >= pages_ || dest_refs_ == nullptr) {
            return false;
        }
        return __atomic_load_n(&dest_refs_[index], __ATOMIC_ACQUIRE) != 0;
    }

} // namespace astia::mm
