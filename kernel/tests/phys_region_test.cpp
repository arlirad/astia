#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/heap.hpp>
#include <astia/mm/iovec.hpp>
#include <astia/mm/phys_region.hpp>
#include <astia/mm/pinned_buffer.hpp>

#include "arena.hpp"
#include "test.hpp"

namespace {
    using astia::mm::phys_region;

    // The heap's arch hook is a no-op on the host: the window is
    // already reachable memory.
    bool fake_map(astia::u64, astia::u64) {
        return true;
    }

    // phys_region takes its frames from the kernel frame allocator and
    // its objects from the heap; point both at the arena like the
    // other substrate-bound tests do, and report the free-frame count
    // so callers can assert full reclamation.
    astia::usz init_world() {
        astia_test::map_arena();

        static bool ready = false;
        if (!ready) {
            astia::mm::memory_map map;

            map.add(astia_test::arena_base, astia_test::arena_size,
                    astia::mm::memory_type::usable);
            astia::mm::kernel_frames().init(map);

            static astia::u8 region[64 * 1024];

            astia::mm::heap::configure(region, sizeof(region), fake_map);
            astia::mm::heap::init();

            ready = true;
        }

        return astia::mm::kernel_frames().available_frames();
    }
} // namespace

ASTIA_TEST(phys_region_create_adopts_frames) {
    const auto before = init_world();
    auto*      region = phys_region::create(4, true);

    ASTIA_CHECK(region != nullptr);
    ASTIA_CHECK(region->pages() == 4);
    ASTIA_CHECK(region->references() == 1);
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before - 4);
    region->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(phys_region_lazy_allocates_pages_on_demand) {
    const auto before = init_world();
    auto*      region = phys_region::create_lazy(4);

    ASTIA_CHECK(region != nullptr);
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
    ASTIA_CHECK(region->page_base(0) == astia::mm::frame_allocator::invalid_frame);
    ASTIA_CHECK(region->page_direct(0) == nullptr);

    bool created = false;

    ASTIA_CHECK(region->ensure_page(2, true, &created));
    ASTIA_CHECK(created);
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before - 1);
    ASTIA_CHECK(region->page_direct(2) != nullptr);
    ASTIA_CHECK(region->ensure_page(2, true, &created));
    ASTIA_CHECK(!created);
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before - 1);

    ASTIA_CHECK(region->unmap_range(2, 1));
    region->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(phys_region_unique_until_shared) {
    init_world();

    auto* region = phys_region::create(2, true);

    ASTIA_CHECK(region != nullptr);
    ASTIA_CHECK(region->uniquely_held(0));
    region->addref(); // a fork would do exactly this
    ASTIA_CHECK(!region->uniquely_held(0));
    ASTIA_CHECK(region->references() == 2);
    region->release();
    ASTIA_CHECK(region->uniquely_held(0)); // sole sharer again
    region->release();
}

ASTIA_TEST(phys_region_split_page_copies_and_tracks) {
    const auto before = init_world();
    auto*      region = phys_region::create(2, true);

    ASTIA_CHECK(region != nullptr);

    auto* bytes = static_cast<astia::u8*>(region->page_direct(1));

    for (astia::usz b = 0; b < phys_region::frame_size; ++b) {
        bytes[b] = static_cast<astia::u8>(b & 0xFF);
    }

    region->addref(); // shared between two address spaces

    auto* child = region->split_page(1);

    ASTIA_CHECK(child != nullptr);
    ASTIA_CHECK(child->pages() == 1);
    ASTIA_CHECK(child->references() == 1);

    const auto* copy = static_cast<const astia::u8*>(child->page_direct(0));

    ASTIA_CHECK(copy[0] == 0 && copy[17] == 17 && copy[4095] == 255);
    // The copier now maps the child, not the parent page: one sharer
    // left on page 1 — but page 0 is still shared.
    ASTIA_CHECK(region->uniquely_held(1));
    ASTIA_CHECK(!region->uniquely_held(0));

    // A second copier of the same page: nobody maps the parent page.
    auto* second = region->split_page(1);

    ASTIA_CHECK(second != nullptr);
    ASTIA_CHECK(!region->uniquely_held(1));
    second->release(); // the second copier exits
    // The remaining copier owns a child page; the parent page has no
    // mapping left, so it is not a writable in-place candidate.
    ASTIA_CHECK(!region->uniquely_held(1));

    child->release();
    region->release();
    region->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(phys_region_copy_page_is_reversible) {
    const auto before = init_world();
    auto*      region = phys_region::create(1, true);

    ASTIA_CHECK(region != nullptr);

    static_cast<astia::u8*>(region->page_direct(0))[0] = 0x5a;

    auto* copy = region->copy_page(0);

    ASTIA_CHECK(copy != nullptr);
    ASTIA_CHECK(region->uniquely_held(0));
    ASTIA_CHECK(static_cast<astia::u8*>(copy->page_direct(0))[0] == 0x5a);
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before - 2);

    copy->release();
    region->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(phys_region_split_rejects_unmaterialized_page) {
    const auto before = init_world();
    auto*      region = phys_region::create_lazy(1);

    ASTIA_CHECK(region != nullptr);
    ASTIA_CHECK(region->split_page(0) == nullptr);
    ASTIA_CHECK(region->copy_page(0) == nullptr);
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
    region->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(phys_region_copy_page_rolls_back_on_allocation_failure) {
    const auto before = init_world();
    auto*      region = phys_region::create(1, true);

    ASTIA_CHECK(region != nullptr);

    if (region == nullptr)
        return;

    const auto frame = region->page_base(0);
    const auto used  = astia::mm::heap::used();
    void*      held[4096]{};
    astia::usz held_count = 0;

    while (held_count < sizeof held / sizeof held[0]) {
        auto block = astia::mm::heap::alloc(16);
        if (!block)
            break;

        held[held_count++] = block.value;
    }

    ASTIA_CHECK(astia::mm::heap::used() > used);
    ASTIA_CHECK(region->copy_page(0) == nullptr);
    ASTIA_CHECK(region->page_base(0) == frame);
    ASTIA_CHECK(region->uniquely_held(0));
    ASTIA_CHECK(region->references() == 1);

    for (astia::usz i = 0; i < held_count; ++i) {
        astia::mm::heap::free(held[i]);
    }

    ASTIA_CHECK(astia::mm::heap::used() == used);
    region->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(phys_region_reclaims_last_unmapped_page) {
    const auto before = init_world();
    auto*      region = phys_region::create(4, true);

    ASTIA_CHECK(region != nullptr);
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before - 4);

    // Two address spaces share the four-page range.  Removing page 1
    // from one leaves its frame alive; removing it from the second
    // returns exactly that frame while the neighbours remain allocated.
    ASTIA_CHECK(region->share_range(0, 4));
    ASTIA_CHECK(region->unmap_range(1, 1));
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before - 4);
    ASTIA_CHECK(region->unmap_range(1, 1));
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before - 3);

    region->release();
    region->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(phys_region_parent_death_detaches_children) {
    const auto before = init_world();
    auto*      region = phys_region::create(1, true);

    ASTIA_CHECK(region != nullptr);

    auto* child = region->split_page(0);

    ASTIA_CHECK(child != nullptr);

    region->release(); // last reference: the parent dies first

    // The child owns its own frames and needs no parent: it must stay // fully usable,
    // and able to CoW again.
    auto* byte = static_cast<astia::u8*>(child->page_direct(0));

    byte[0] = 0x77;

    ASTIA_CHECK(byte[0] == 0x77);

    auto* grandchild = child->split_page(0);

    ASTIA_CHECK(grandchild != nullptr);
    grandchild->release();
    child->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(pinned_buffer_holds_frames_across_vma_release) {
    const auto before = init_world();
    auto*      region = phys_region::create(4, true);

    ASTIA_CHECK(region != nullptr);
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before - 4);

    astia::mm::pinned_buffer pin;

    ASTIA_CHECK(pin.acquire(region, 1, 2, false));
    ASTIA_CHECK(pin.valid());
    ASTIA_CHECK(pin.pages() == 2);
    ASTIA_CHECK(region->references() == 2); // coverage + allocation reference

    // The mapping goes away while the operation is in flight.  The two frames
    // it referenced must survive — and so must the region object that holds
    // the counters the pin will later decrement.  (Holding only the coverage
    // would free the object here and leave release() writing into freed
    // memory.)
    region->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before - 2);

    pin.release();
    ASTIA_CHECK(!pin.valid());
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);

    pin.release(); // idempotent, and safe on an empty buffer
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(pinned_buffer_exposes_one_contiguous_window) {
    const auto before = init_world();
    auto*      region = phys_region::create(4, true);

    ASTIA_CHECK(region != nullptr);

    astia::mm::pinned_buffer pin;

    ASTIA_CHECK(pin.acquire(region, 1, 3, false));

    // A region is a contiguous frame run and the direct map is linear, so the
    // pinned range is a single kernel window.  That is the property the ring
    // leans on to hand a device one base address instead of a scatter list.
    auto* base = static_cast<astia::u8*>(pin.direct(0));

    ASTIA_CHECK(base == region->page_direct(1));

    for (astia::usz page = 0; page < pin.pages(); ++page) {
        ASTIA_CHECK(pin.direct(page) == base + page * phys_region::frame_size);
        ASTIA_CHECK(pin.phys(page) ==
                    region->base() + (1 + page) * phys_region::frame_size);
    }

    pin.release();
    region->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(pinned_buffer_page_accessors_bound_the_index) {
    const auto before = init_world();
    auto*      region = phys_region::create(4, true);

    ASTIA_CHECK(region != nullptr);

    if (region == nullptr)
        return;

    astia::mm::pinned_buffer pin;

    ASTIA_CHECK(pin.acquire(region, 1, 2, false));

    // The last pinned page is still addressable, and names the region's frame.
    ASTIA_CHECK(pin.phys(0) == region->page_base(1));
    ASTIA_CHECK(pin.phys(1) == region->page_base(2));
    ASTIA_CHECK(pin.direct(1) == region->page_direct(2));

    // One past the pin is rejected, not resolved to the region's next frame:
    // invalid_frame is all bits set, which no page-aligned frame can be.
    ASTIA_CHECK(pin.phys(pin.pages()) == astia::mm::frame_allocator::invalid_frame);
    ASTIA_CHECK(pin.phys(pin.pages()) != region->page_base(3));
    ASTIA_CHECK(pin.direct(pin.pages()) == nullptr);

    // A huge index must be rejected before _first + index can wrap back
    // into the pin.
    ASTIA_CHECK(pin.phys(~astia::usz{0}) == astia::mm::frame_allocator::invalid_frame);
    ASTIA_CHECK(pin.direct(~astia::usz{0}) == nullptr);

    // An empty pin has no page 0 at all.
    pin.release();
    ASTIA_CHECK(pin.phys(0) == astia::mm::frame_allocator::invalid_frame);
    ASTIA_CHECK(pin.direct(0) == nullptr);

    region->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(pinned_buffer_rejects_ranges_outside_the_region) {
    const auto before = init_world();
    auto*      region = phys_region::create(2, true);

    ASTIA_CHECK(region != nullptr);

    astia::mm::pinned_buffer pin;

    ASTIA_CHECK(!pin.acquire(nullptr, 0, 1, true));
    ASTIA_CHECK(!pin.acquire(region, 0, 0, true));
    ASTIA_CHECK(!pin.acquire(region, 2, 1, true)); // first page past the end
    ASTIA_CHECK(!pin.acquire(region, 1, 2, true)); // runs off the end
    ASTIA_CHECK(!pin.valid());
    ASTIA_CHECK(!region->destination_pinned(0)); // a rejected pin marks nothing
    ASTIA_CHECK(!region->destination_pinned(1));

    // A rejected acquire references nothing and takes nothing.
    ASTIA_CHECK(region->references() == 1);
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before - 2);

    pin.release(); // empty: a no-op
    region->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(pinned_buffer_rejects_second_acquire_without_disturbing_the_pin) {
    const auto before = init_world();
    auto*      held   = phys_region::create(4, true);
    auto*      other  = phys_region::create(2, true);

    ASTIA_CHECK(held != nullptr);
    ASTIA_CHECK(other != nullptr);

    if (held == nullptr || other == nullptr)
        return;

    astia::mm::pinned_buffer pin;

    ASTIA_CHECK(pin.acquire(held, 1, 2, true));
    ASTIA_CHECK(held->references() == 2); // coverage + allocation reference

    // A live pin cannot be overwritten: the second acquire must fail and
    // leave the first region's references and destination marks intact.
    ASTIA_CHECK(!pin.acquire(other, 0, 1, true));
    ASTIA_CHECK(pin.valid());
    ASTIA_CHECK(pin.pages() == 2);
    ASTIA_CHECK(pin.direct(0) == held->page_direct(1));
    ASTIA_CHECK(pin.phys(0) == held->base() + phys_region::frame_size);
    ASTIA_CHECK(held->references() == 2);
    ASTIA_CHECK(held->destination_pinned(1));
    ASTIA_CHECK(held->destination_pinned(2));
    ASTIA_CHECK(other->references() == 1);
    ASTIA_CHECK(!other->destination_pinned(0));
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before - 6);

    pin.release();
    held->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before - 2);

    other->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(pinned_buffer_reacquires_after_release) {
    const auto before = init_world();
    auto*      first  = phys_region::create(2, true);
    auto*      second = phys_region::create(2, true);

    ASTIA_CHECK(first != nullptr);
    ASTIA_CHECK(second != nullptr);

    if (first == nullptr || second == nullptr)
        return;

    astia::mm::pinned_buffer pin;

    ASTIA_CHECK(pin.acquire(first, 0, 1, false));
    ASTIA_CHECK(!pin.acquire(second, 0, 1, false));
    ASTIA_CHECK(pin.direct(0) == first->page_direct(0));

    pin.release();
    ASTIA_CHECK(!pin.valid());
    ASTIA_CHECK(first->references() == 1);
    ASTIA_CHECK(second->references() == 1);

    ASTIA_CHECK(pin.acquire(second, 1, 1, true));
    ASTIA_CHECK(pin.direct(0) == second->page_direct(1));
    ASTIA_CHECK(second->references() == 2);
    ASTIA_CHECK(second->destination_pinned(1));

    pin.release();
    first->release();
    second->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(pinned_buffer_destination_marks_are_counted) {
    const auto before = init_world();
    auto*      region = phys_region::create(3, true);

    ASTIA_CHECK(region != nullptr);

    // Two operations are filling the same two pages; a third is draining
    // them.  Only the filling ones need the frames held still: the drain
    // reads whatever is in the frame, and a CoW copy would hold the same
    // bytes.
    astia::mm::pinned_buffer first;
    astia::mm::pinned_buffer second;
    astia::mm::pinned_buffer drain;

    ASTIA_CHECK(first.acquire(region, 0, 2, true));
    ASTIA_CHECK(second.acquire(region, 0, 2, true));
    ASTIA_CHECK(drain.acquire(region, 1, 2, false));

    ASTIA_CHECK(region->destination_pinned(0));
    ASTIA_CHECK(region->destination_pinned(1));
    ASTIA_CHECK(!region->destination_pinned(2)); // drained, not filled

    // Counted: one release does not clear a mark another pin still holds.
    first.release();
    ASTIA_CHECK(region->destination_pinned(0));
    ASTIA_CHECK(region->destination_pinned(1));
    second.release();
    ASTIA_CHECK(!region->destination_pinned(0));
    ASTIA_CHECK(!region->destination_pinned(1));

    drain.release();
    region->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(phys_region_destination_release_is_idempotent) {
    const auto before = init_world();
    auto*      region = phys_region::create(2, true);

    ASTIA_CHECK(region != nullptr);

    ASTIA_CHECK(!region->retain_destination(2, 1)); // first page past the end
    ASTIA_CHECK(!region->retain_destination(1, 2)); // runs off the end
    ASTIA_CHECK(!region->destination_pinned(0));
    ASTIA_CHECK(!region->destination_pinned(1));

    ASTIA_CHECK(region->retain_destination(0, 2));
    ASTIA_CHECK(region->destination_pinned(0));
    ASTIA_CHECK(region->destination_pinned(1));
    region->release_destination(0, 2);
    ASTIA_CHECK(!region->destination_pinned(0));
    ASTIA_CHECK(!region->destination_pinned(1));
    // A second release must not wrap the counter to "pinned" again.
    region->release_destination(0, 2);
    ASTIA_CHECK(!region->destination_pinned(0));
    ASTIA_CHECK(!region->destination_pinned(1));

    region->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(phys_region_split_does_not_carry_destination_marks) {
    // The mark is what makes cow_fault() refuse a split, so a split that
    // did happen must at least not pretend the copy is pinned: the copy is
    // a different frame, and the operation's frame is still the parent's.
    const auto before = init_world();
    auto*      region = phys_region::create(2, true);

    ASTIA_CHECK(region != nullptr);

    astia::mm::pinned_buffer pin;

    ASTIA_CHECK(pin.acquire(region, 1, 1, true));
    ASTIA_CHECK(region->destination_pinned(1));

    auto* copy = region->split_page(1);

    ASTIA_CHECK(copy != nullptr);
    ASTIA_CHECK(!copy->destination_pinned(0));  // the copy is a fresh frame
    ASTIA_CHECK(region->destination_pinned(1)); // the pinned frame keeps its mark

    pin.release();
    ASTIA_CHECK(!region->destination_pinned(1));
    copy->release();
    region->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(iovec_holds_every_entry_across_vma_release) {
    const auto before = init_world();
    auto*      first  = phys_region::create(4, true);
    auto*      second = phys_region::create(2, true);

    ASTIA_CHECK(first != nullptr && second != nullptr);
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before - 6);

    astia::mm::iovec list;

    // Two entries in different regions, with a zero-length entry between them:
    // legal in a writev(), and it must leave no trace at all.
    ASTIA_CHECK(list.append(first, 1, 2, 0, 8192, false));
    ASTIA_CHECK(list.append(second, 0, 1, 0, 0, false));
    ASTIA_CHECK(list.append(second, 1, 1, 24, 40, false));
    ASTIA_CHECK(list.count() == 2);
    ASTIA_CHECK(list.bytes() == 8192 + 40);

    // Both mappings go away while the operation is in flight.  The frames the
    // entries referenced must survive, and so must the region objects that
    // hold the counters release() will decrement.
    first->release();
    second->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before - 3);

    list.release();
    ASTIA_CHECK(list.count() == 0);
    ASTIA_CHECK(list.empty());
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);

    list.release(); // idempotent, and safe on an empty list
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(iovec_cursor_walks_entries_in_kernel_address_space) {
    const auto before = init_world();
    auto*      region = phys_region::create(4, true);

    ASTIA_CHECK(region != nullptr);

    astia::mm::iovec list;

    ASTIA_CHECK(list.append(region, 0, 2, 16, 5000, false)); // crosses pages 0..1
    ASTIA_CHECK(list.append(region, 2, 1, 0, 100, false));
    ASTIA_CHECK(list.bytes() == 5100);

    list.aim(list.bytes());
    ASTIA_CHECK(!list.done());
    ASTIA_CHECK(list.remaining() == 5100);

    // The window names the entry's first byte, not the page it sits on: the
    // in-page offset is part of the operation, not of the pin.
    ASTIA_CHECK(list.window() == static_cast<char*>(region->page_direct(0)) + 16);
    ASTIA_CHECK(list.chunk() == 5000);
    ASTIA_CHECK(list.advance(4080) == 4080); // rest of page 0
    ASTIA_CHECK(list.remaining() == 1020);

    // Same entry, now running on into its second page — and it runs on into
    // the frame that actually follows, because a region is contiguous.
    ASTIA_CHECK(list.window() == static_cast<char*>(region->page_direct(1)));
    ASTIA_CHECK(list.chunk() == 920);
    ASTIA_CHECK(list.advance(920) == 920); // rest of entry 0
    ASTIA_CHECK(list.window() == static_cast<char*>(region->page_direct(2)));
    ASTIA_CHECK(list.chunk() == 100);
    ASTIA_CHECK(list.advance(100) == 100);
    ASTIA_CHECK(list.done());
    ASTIA_CHECK(list.remaining() == 0);
    ASTIA_CHECK(list.window() == nullptr);
    ASTIA_CHECK(list.advance(64) == 0); // nothing left to consume

    region->release();
    list.release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(iovec_refuses_what_it_cannot_hold) {
    const auto before = init_world();
    auto*      region = phys_region::create(2, true);

    ASTIA_CHECK(region != nullptr);

    astia::mm::iovec list;

    // Neither an entry that addresses bytes outside the frames the pin holds
    // nor one that starts past them may be recorded.
    ASTIA_CHECK(!list.append(region, 0, 1, 0, 4097, false));
    ASTIA_CHECK(!list.append(region, 0, 1, 4096, 1, false));
    ASTIA_CHECK(list.empty());
    ASTIA_CHECK(region->references() == 1); // the refusals retained nothing

    for (astia::usz i = 0; i < astia::mm::iovec::max_segments; ++i) {
        ASTIA_CHECK(list.append(region, 0, 1, 0, 16, false));
    }

    ASTIA_CHECK(list.count() == astia::mm::iovec::max_segments);
    ASTIA_CHECK(!list.append(region, 0, 1, 0, 16, false)); // no room left
    ASTIA_CHECK(list.count() == astia::mm::iovec::max_segments);

    list.release();
    region->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(iovec_walks_a_kernel_entry_like_any_other) {
    const auto before = init_world();
    auto*      region = phys_region::create(2, true);

    char local[8];

    ASTIA_CHECK(region != nullptr);

    astia::mm::iovec list;

    ASTIA_CHECK(list.append(region, 0, 1, 8, 100, false));
    ASTIA_CHECK(list.append_kernel(local, sizeof local));
    ASTIA_CHECK(list.count() == 2);
    ASTIA_CHECK(list.bytes() == 100 + sizeof local);

    // Appending armed the operation: nothing has to call aim() for a freshly
    // built list to be walkable.  Without this the window would be empty and
    // every operation would park forever.
    ASTIA_CHECK(list.remaining() == list.bytes());
    ASTIA_CHECK(list.window() == static_cast<char*>(region->page_direct(0)) + 8);
    ASTIA_CHECK(list.advance(100) == 100);
    // The next entry is an in-kernel caller's buffer: there is nothing to
    // pin, so its window is simply the pointer it handed us.  The cursor
    // still walks it like any other entry.
    ASTIA_CHECK(list.window() == local);
    ASTIA_CHECK(list.chunk() == sizeof local);
    ASTIA_CHECK(list.advance(sizeof local) == sizeof local);
    ASTIA_CHECK(list.done());

    region->release();
    list.release();
    ASTIA_CHECK(list.count() == 0);
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}
