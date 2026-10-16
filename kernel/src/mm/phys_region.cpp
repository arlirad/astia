#include <astia/mm/phys_region.hpp>

#include <astia/mm/heap.hpp>

namespace astia::mm {

    namespace {
        constexpr u16 max_page_mappings = ~u16{0};
    } // namespace

    auto phys_region::initialize(u64 base, usize pages) -> bool {
            if (pages > (~usize{0}) / sizeof(u16)) {
                return false;
            }
            auto storage = heap::alloc(pages * sizeof(u16));
            if (!storage) {
                return false;
            }
            base_      = base;
            pages_     = pages;
            page_refs_ = static_cast<u16*>(static_cast<void*>(storage.value));
            // A freshly-created/adopted region carries its initial
            // mapping reference.  This preserves the existing ownership
            // contract: the creator transfers that reference to its VMA.
            for (usize i = 0; i < pages; ++i) {
                page_refs_[i] = 1;
            }
            return true;
    }

    auto phys_region::create(usize page_count, bool zero) -> phys_region* {
        if (page_count == 0) {
            return nullptr;
        }
        const u64 base = kernel_frames().alloc(
            page_count, zero ? frame_flags::zeroed : frame_flags::none);
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

    auto phys_region::adopt(u64 base, usize page_count) -> phys_region* {
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

    auto phys_region::retain_mapping(usize first, usize count) -> bool {
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
                if (__atomic_compare_exchange_n(slot, &old, next, false,
                                                __ATOMIC_ACQ_REL,
                                                __ATOMIC_ACQUIRE)) {
                    break;
                }
            }
        }
        return true;
    }

    auto phys_region::unmap_range(usize first, usize count) -> bool {
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
                if (__atomic_compare_exchange_n(slot, &old, next, false,
                                                __ATOMIC_ACQ_REL,
                                                __ATOMIC_ACQUIRE)) {
                    if (old == 1) {
                        kernel_frames().release(base_ + (first + i) * frame_size);
                    }
                    break;
                }
            }
        }
        return true;
    }

    auto phys_region::share_range(usize first, usize count) -> bool {
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

    auto phys_region::addref() -> bool {
        // Legacy full-range sharing API.  New VMA code uses share_range()
        // so partial file mappings do not charge unrelated pages.
        return share_range(0, pages_);
    }

    auto phys_region::references() -> u32 {
        core::spinlock_guard guard(lock_);
        return refs_;
    }

    auto phys_region::page_direct(usize index) -> void* {
        return kernel_frames().to_virt(base_ + index * frame_size);
    }

    auto phys_region::release_ref() -> void {
        child* orphans = nullptr;
        {
            core::spinlock_guard guard(lock_);
            if (refs_ == 0) {
                return;
            }
            if (refs_ > 1) {
                --refs_;
                return;
            }
            refs_    = 0;
            orphans  = children_;
            children_ = nullptr;
        }

        while (orphans != nullptr) {
            child* node = orphans;
            orphans     = node->next;
            node->region->detach_parent(this);
            heap::free(node);
        }
        if (parent_ != nullptr) {
            parent_->unlink_child(this);
            parent_ = nullptr;
        }

        // Any still-live pages are owned by this last allocation
        // reference.  Normally their mapping counters are already zero;
        // releasing here is the defensive final cleanup.
        for (usize i = 0; i < pages_; ++i) {
            u16 old = __atomic_exchange_n(&page_refs_[i], u16{0},
                                          __ATOMIC_ACQ_REL);
            if (old != 0) {
                kernel_frames().release(base_ + i * frame_size);
            }
        }
        heap::free(page_refs_);
        heap::free(this);
    }

    auto phys_region::release_range(usize first, usize count) -> bool {
        if (!unmap_range(first, count)) {
            return false;
        }
        release_ref();
        return true;
    }

    auto phys_region::release() -> void {
        // Legacy full-range release.  VMA teardown uses unmap_range for
        // every surviving sibling and then release_ref() once.
        (void) unmap_range(0, pages_);
        release_ref();
    }

    auto phys_region::unlink_child(phys_region* target) -> void {
        core::spinlock_guard guard(lock_);
        child**             link = &children_;
        while (*link != nullptr) {
            if ((*link)->region == target) {
                child* dead = *link;
                *link       = dead->next;
                heap::free(dead);
                return;
            }
            link = &(*link)->next;
        }
    }

    auto phys_region::detach_parent(const phys_region* expected) -> void {
        core::spinlock_guard guard(lock_);
        if (parent_ == expected) {
            parent_ = nullptr;
        }
    }

    auto phys_region::split_page(usize index) -> phys_region* {
        if (index >= pages_ || page_refs_ == nullptr ||
            __atomic_load_n(&page_refs_[index], __ATOMIC_ACQUIRE) == 0) {
            return nullptr;
        }
        auto* copy = create(1, false); // the bytes come over below
        if (copy == nullptr) {
            return nullptr;
        }
        auto node = heap::alloc(sizeof(child));
        if (!node) {
            copy->release();
            return nullptr;
        }

        const auto* source = static_cast<const u8*>(page_direct(index));
        auto*       target = static_cast<u8*>(copy->page_direct(0));
        for (usize b = 0; b < frame_size; ++b) {
            target[b] = source[b];
        }

        // The caller is moving one mapping from this page to `copy`.
        // This makes split_page useful both to the kernel fault path and
        // to the low-level phys_region tests without a separate global
        // frame-use table.
        if (!unmap_range(index, 1)) {
            heap::free(node.value);
            copy->release();
            return nullptr;
        }

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

    auto phys_region::uniquely_held(usize index) -> bool {
        if (index >= pages_ || page_refs_ == nullptr) {
            return false;
        }
        return __atomic_load_n(&page_refs_[index], __ATOMIC_ACQUIRE) == 1;
    }

} // namespace astia::mm
