#include "aio.hpp"

#include "proc.hpp"
#include "user_mem.hpp"
#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/memory.hpp>

void* operator new(astia::usize size, void* where) noexcept;

/*
 * The ring engine.  Layout, cursors and ordering follow
 * <astia/api/aio.h> exactly.  The filesystem owns polymorphic file
 * instances; this layer only translates the userspace ABI's integer
 * handles to mm::shptr<fs::open_file> objects and keeps parked operations alive.
 *
 * What executes an SQE:
 *   NOP            -> completes with 0
 *   WRITE          -> file::write
 *   READ           -> file::read; zero parks until an IRQ-side nudge
 *   OPENAT         -> fs::open, installs the returned open description
 *   CLOSE          -> releases the ring's reference to the file
 *   DUP/DUP2       -> aliases the same open description
 *   STATX          -> size() of the target, fstat or path form; the
 *                     metadata the VFS does not carry (mode, times) is 0
 *   anything else  -> -ENOSYS
 *
 * Userspace reach: the ring itself lives in kernel .bss, which the higher
 * half maps supervisor-only, so a process asking for a ring gets its own
 * user-writable alias of the same frames mapped into its address space
 * (io_setup). In-kernel clients — no address space of their own — keep
 * using the kernel address; both spell the same physical pages, so the
 * cursor protocol is identical either way.
 */

namespace astia::sys {

    namespace {

        constexpr u32 ring_count    = 2;
        constexpr u32 park_capacity = 16;

        constexpr usize map_bytes = sizeof(astia_rings) + sizeof(astia_sq_ring) +
                                    sizeof(astia_cq_ring) +
                                    sizeof(astia_sqe) * io_ring_max_entries +
                                    sizeof(astia_cqe) * (2 * io_ring_max_entries);

        // User-half window a ring is aliased into when a *process* sets one
        // up: the ring frames live in kernel .bss (higher half, supervisor
        // only), so an userspace producer needs its own user-accessible
        // view of the same pages. 64 KiB per slot covers map_bytes (two
        // 4 KiB pages at today's geometry) with room to grow, and the window
        // sits well above the user image (0x40000000) and below the user
        // stack (0x7ffffff000).
        constexpr u64 user_ring_base  = 0x0000700000000000ull;
        constexpr u64 user_ring_span  = 0x000000010000ull;
        constexpr u32 user_ring_pages = static_cast<u32>((map_bytes + 4095u) / 4096u);

        struct parked_op {
            bool                     used;
            i32                      fd;
            mm::shptr<fs::open_file> object;
            void*                    buf;
            u32                      len;
            u64                      off;
            u64                      user_data;
        };

        struct ring_slot {
            bool      used;
            u64       user_base; // user-half alias of the ring; 0 = not mapped
            parked_op park[park_capacity];
            // Page-aligned: aliasing the ring into a process maps whole
            // frames, so the source address — and with it the physical
            // address translate() hands map_4k() — must fall on a page
            // boundary. 64 bytes is not enough.  Park state deliberately
            // sits in front of it, outside the two pages userspace sees.
            alignas(4096) u8 map[map_bytes];
        };

        // Raw pointers keep the pre-heap global state trivial.  A ring
        // slot owns shptr members and is constructed in this fixed,
        // aligned pool; no global shptr destructors are registered.
        ring_slot* g_slots[ring_count];
        alignas(4096) u8 slot_storage[ring_count][sizeof(ring_slot)];

        static_assert(alignof(ring_slot) >= 4096,
                      "ring slots must be page-granular (see ring_slot::map)");
        static_assert(map_bytes <= sizeof(ring_slot::map),
                      "ring geometry exceeds the ring_slot backing array");

        astia_rings* rings_of(const ring_slot& slot) {
            return reinterpret_cast<astia_rings*>(const_cast<u8*>(slot.map));
        }

        ring_slot* slot_of(const astia_rings* ring) {
            const u64 address = reinterpret_cast<u64>(ring);
            for (u32 i = 0; i < ring_count; ++i) {
                // The user alias carries the ring's intra-page offset, so
                // match any address inside the slot's aliased page range.
                const u64 alias_span = static_cast<u64>(user_ring_pages) * 4096u;
                if (g_slots[i] != nullptr && g_slots[i]->used &&
                    (reinterpret_cast<u64>(rings_of(*g_slots[i])) == address ||
                     (g_slots[i]->user_base != 0 &&
                      address - g_slots[i]->user_base < alias_span))) {
                    return g_slots[i];
                }
            }
            return nullptr;
        }

        void clear_slot(ring_slot& slot) {
            for (u32 i = 0; i < park_capacity; ++i) {
                slot.park[i] = parked_op{};
            }
            __builtin_memset(slot.map, 0, sizeof slot.map);
            slot.user_base = 0;
            slot.used      = false;
        }

        /// Alias a slot's ring frames into the process' user half and return
        /// the address userspace must use through `*out`. Nothing is mapped
        /// if any page fails: the partial mapping is removed first.
        long map_ring_for_user(ring_slot& slot, u32 index, proc::process* owner,
                               u64* out) {
            arch::page_map* space = owner == nullptr ? nullptr : owner->address_space();
            const arch::page_map* kernel = arch::kernel_page_map();
            if (space == nullptr || kernel == nullptr) {
                return -ESRCH;
            }
            const u64 base  = user_ring_base + user_ring_span * index;
            const u64 first = reinterpret_cast<u64>(rings_of(slot));
            for (u32 page = 0; page < user_ring_pages; ++page) {
                const u64 offset = static_cast<u64>(page) * 4096u;
                // translate() folds in the address' page offset, and the
                // ring storage is not page-aligned: mask it back off.
                const u64 phys = kernel->translate(first + offset) & ~u64{0xfff};
                if (!space->map_4k(base + offset, phys,
                                   arch::page_flags::present |
                                       arch::page_flags::writable |
                                       arch::page_flags::user)) {
                    for (u32 done = 0; done < page; ++done) {
                        (void) space->unmap(base + static_cast<u64>(done) * 4096u);
                    }
                    return -ENOMEM;
                }
            }
            // The mapping is page-granular while the ring struct starts
            // mid-page: carry the intra-page offset into the alias so
            // `base + ...` addresses inside the struct stay correct.
            slot.user_base = base;
            *out           = base + (first & 0xfff);
            return 0;
        }

        /// Drop the user-half alias of a slot, if it has one. The frames
        /// themselves are kernel .bss and stay; only the process' view goes.
        void unmap_ring_user(ring_slot& slot, proc::process* owner) {
            if (slot.user_base == 0) {
                return;
            }
            arch::page_map* space = owner == nullptr ? nullptr : owner->address_space();
            if (space != nullptr) {
                for (u32 page = 0; page < user_ring_pages; ++page) {
                    (void) space->unmap(slot.user_base + static_cast<u64>(page) * 4096u);
                }
            }
            slot.user_base = 0;
        }

        bool cq_post(astia_rings* ring, u64 user_data, i32 result) {
            astia_cq_ring* cq   = astia_rings_cq(ring);
            const u32      head = __atomic_load_n(&cq->head, __ATOMIC_ACQUIRE);
            const u32      tail = __atomic_load_n(&cq->tail, __ATOMIC_RELAXED);
            if (tail - head >= cq->entries) {
                return false;
            }
            astia_cqe* c = &astia_rings_cqes(ring)[tail & cq->mask];
            c->user_data = user_data;
            c->result    = result;
            c->flags     = 0;
            __atomic_store_n(&cq->tail, tail + 1, __ATOMIC_RELEASE);
            return true;
        }

        bool cq_post_guarded(astia_rings* ring, u64 user_data, i32 result) {
            arch::interrupt_guard gate;
            return cq_post(ring, user_data, result);
        }

        mm::shptr<fs::open_file> file_for(i32 fd) {
            proc::process* current = proc::current_process();
            return current == nullptr ? mm::shptr<fs::open_file>{}
                                      : current->descriptor(fd);
        }

        void retry_parked(astia_rings* ring, parked_op& op) {
            const long n = op.object->read(op.buf, op.len, op.off);
            if (n == 0) {
                return;
            }
            // A full CQ leaves the operation and its shared file
            // reference parked for the next IRQ nudge.
            if (cq_post(ring, op.user_data, static_cast<i32>(n))) {
                op.object = mm::shptr<fs::open_file>{};
                op.used   = false;
            }
        }

        bool dispatch_one(ring_slot& slot, astia_rings* ring, const astia_sqe& s) {
            switch (s.op) {
            case OP_NOP:
                return cq_post_guarded(ring, s.user_data, 0);

            case OP_WRITE: {
                mm::shptr<fs::open_file> object = file_for(s.fd);
                if (!object) {
                    return cq_post_guarded(ring, s.user_data, -EBADF);
                }
                const auto* buf =
                    reinterpret_cast<const void*>(static_cast<usize>(s.address));
                if (!sys::user_range_ok(buf, s.length)) {
                    return cq_post_guarded(ring, s.user_data, -EFAULT);
                }
                const long n = object->write(buf, s.length, s.offset);
                return cq_post_guarded(ring, s.user_data, static_cast<i32>(n));
            }

            case OP_READ: {
                mm::shptr<fs::open_file> object = file_for(s.fd);
                if (!object) {
                    return cq_post_guarded(ring, s.user_data, -EBADF);
                }
                auto* buf = reinterpret_cast<void*>(static_cast<usize>(s.address));
                // Validated at submission; the buffer is not pinned across
                // a park, so a concurrent unmap from another thread of the
                // same process is still on the pinning worklist.
                if (!sys::user_range_ok(buf, s.length)) {
                    return cq_post_guarded(ring, s.user_data, -EFAULT);
                }
                const long n = object->read(buf, s.length, s.offset);
                if (n != 0) {
                    return cq_post_guarded(ring, s.user_data, static_cast<i32>(n));
                }
                arch::interrupt_guard gate;
                for (u32 i = 0; i < park_capacity; ++i) {
                    parked_op& p = slot.park[i];
                    if (!p.used) {
                        p.used      = true;
                        p.fd        = s.fd;
                        p.object    = object;
                        p.buf       = buf;
                        p.len       = s.length;
                        p.off       = s.offset;
                        p.user_data = s.user_data;
                        return true;
                    }
                }
                return cq_post(ring, s.user_data, -EBUSY);
            }

            case OP_OPENAT: {
                // The pathname is user memory: copy it into the kernel
                // rather than letting the VFS walk a raw user pointer.
                char path[256];
                if (!sys::copy_in_string(
                        path, sizeof path,
                        reinterpret_cast<const void*>(static_cast<usize>(s.address)))) {
                    return cq_post_guarded(ring, s.user_data, -EFAULT);
                }
                // op_flags carries O_*; aux is the creation mode.  The
                // VFS needs the former to implement O_CREAT/O_TRUNC on
                // writable filesystems, while mode remains metadata for
                // a future permission layer.
                mm::shptr<fs::open_file> object = fs::open(path, s.op_flags);
                if (!object) {
                    return cq_post_guarded(ring, s.user_data, -ENOENT);
                }
                proc::process* current = proc::current_process();
                if (current == nullptr) {
                    return cq_post_guarded(ring, s.user_data, -ESRCH);
                }
                for (i32 i = 3; i < static_cast<i32>(proc::max_process_fds); ++i) {
                    if (!current->descriptor(i) && current->set_descriptor(i, object)) {
                        return cq_post_guarded(ring, s.user_data, i);
                    }
                }
                return cq_post_guarded(ring, s.user_data, -EMFILE);
            }

            case OP_CLOSE: {
                proc::process* current = proc::current_process();
                if (current == nullptr || !current->descriptor(s.fd)) {
                    return cq_post_guarded(ring, s.user_data, -EBADF);
                }
                current->clear_descriptor(s.fd);
                return cq_post_guarded(ring, s.user_data, 0);
            }

            case OP_DUP: {
                proc::process*           current = proc::current_process();
                mm::shptr<fs::open_file> source  = file_for(s.fd);
                if (current == nullptr || !source) {
                    return cq_post_guarded(ring, s.user_data, -EBADF);
                }
                const i32 fd = current->duplicate_descriptor(s.fd);
                if (fd >= 0) {
                    return cq_post_guarded(ring, s.user_data, fd);
                }
                return cq_post_guarded(ring, s.user_data, -EMFILE);
            }

            case OP_DUP2: {
                proc::process*           current = proc::current_process();
                mm::shptr<fs::open_file> source  = file_for(s.fd);
                if (current == nullptr || !source || s.offset >= proc::max_process_fds) {
                    return cq_post_guarded(ring, s.user_data, -EBADF);
                }
                const i32 target = static_cast<i32>(s.offset);
                if (target != s.fd) {
                    current->set_descriptor(target, source);
                }
                return cq_post_guarded(ring, s.user_data, target);
            }

            case OP_STATX: {
                // Footnote ‡ of <astia/api/aio.h>: address NULL means fd is
                // the stat target (fstat), otherwise address is a pathname
                // and fd is the dirfd. The VFS still has one exact-path
                // namespace, so the dirfd is accepted and ignored.
                auto* out = reinterpret_cast<astia_stat*>(static_cast<usize>(s.offset));
                if (!sys::user_range_ok(out, sizeof(astia_stat))) {
                    return cq_post_guarded(ring, s.user_data, -EFAULT);
                }
                if (s.length != sizeof(astia_stat)) {
                    return cq_post_guarded(ring, s.user_data, -EINVAL);
                }

                mm::shptr<fs::open_file> object;
                if (s.address != 0) {
                    char path[256];
                    if (!sys::copy_in_string(path, sizeof path,
                                             reinterpret_cast<const void*>(
                                                 static_cast<usize>(s.address)))) {
                        return cq_post_guarded(ring, s.user_data, -EFAULT);
                    }
                    object = fs::open_path(path);
                    if (!object) {
                        return cq_post_guarded(ring, s.user_data, -ENOENT);
                    }
                }
                else {
                    object = file_for(s.fd);
                    if (!object) {
                        return cq_post_guarded(ring, s.user_data, -EBADF);
                    }
                }

                const long size = object->size();
                if (size < 0) {
                    return cq_post_guarded(ring, s.user_data, static_cast<i32>(size));
                }
                // The VFS carries a size and nothing else about a node, so
                // mode/uid/times stay zero rather than being guessed.
                __builtin_memset(out, 0, sizeof *out);
                out->size   = static_cast<u64>(size);
                out->blocks = (static_cast<u64>(size) + 511u) / 512u;
                return cq_post_guarded(ring, s.user_data, 0);
            }

            default:
                return cq_post_guarded(ring, s.user_data, -ENOSYS);
            }
        }

    } // namespace

    long io_setup(u32 entries, u32 flags, astia_rings** out) {
        if (out == nullptr || flags != 0 || entries == 0 ||
            entries > io_ring_max_entries) {
            return -EINVAL;
        }

        u32 sq = 1;
        while (sq < entries) {
            sq <<= 1;
        }
        const u32 cq = sq * 2 < 64 ? 64 : sq * 2;

        for (u32 i = 0; i < ring_count; ++i) {
            ring_slot* slot_ptr = g_slots[i];
            if (slot_ptr != nullptr && slot_ptr->used) {
                continue;
            }
            if (slot_ptr == nullptr) {
                slot_ptr   = ::new (slot_storage[i]) ring_slot{};
                g_slots[i] = slot_ptr;
            }
            ring_slot& slot = *slot_ptr;
            clear_slot(slot);
            slot.used = true;

            astia_rings* r = rings_of(slot);
            r->magic       = RINGS_MAGIC;
            r->version     = API_VERSION;
            r->features    = FEAT_SINGLE_MMAP;
            r->sq_entries  = sq;
            r->cq_entries  = cq;
            r->sq_off      = sizeof(astia_rings);
            r->cq_off      = r->sq_off + sizeof(astia_sq_ring);
            r->sqes_off    = r->cq_off + sizeof(astia_cq_ring);
            r->cqes_off    = r->sqes_off + static_cast<u32>(sizeof(astia_sqe)) * sq;

            astia_sq_ring* sqp = astia_rings_sq(r);
            sqp->mask          = sq - 1;
            sqp->entries       = sq;
            astia_cq_ring* cqp = astia_rings_cq(r);
            cqp->mask          = cq - 1;
            cqp->entries       = cq;

            // A process gets a user-writable alias of these frames; a
            // kernel-only client keeps the kernel address.
            astia_rings*   visible = r;
            proc::process* owner   = proc::current_process();
            if (owner != nullptr && owner->address_space() != nullptr) {
                u64        user_address = 0;
                const long rc = map_ring_for_user(slot, i, owner, &user_address);
                if (rc < 0) {
                    clear_slot(slot);
                    return rc;
                }
                visible = reinterpret_cast<astia_rings*>(user_address);
            }

            *out = visible;
            return 0;
        }
        return -ENFILE;
    }

    long io_destroy(astia_rings* ring) {
        ring_slot* slot = slot_of(ring);
        if (slot == nullptr) {
            return -EINVAL;
        }
        arch::interrupt_guard gate;
        for (u32 i = 0; i < park_capacity; ++i) {
            if (slot->park[i].used) {
                return -EBUSY;
            }
        }
        for (u32 i = 0; i < ring_count; ++i) {
            if (g_slots[i] == slot) {
                g_slots[i] = nullptr;
                break;
            }
        }
        // Drop the userspace view (if the ring had one) before the slot
        // goes back to the pool; the frames are kernel .bss and survive.
        unmap_ring_user(*slot, proc::current_process());
        clear_slot(*slot);
        slot->~ring_slot();
        return 0;
    }

    long io_enter(astia_rings* ring, u32 to_submit, u32 min_complete, u32 flags,
                  u64 timeout_ticks) {
        ring_slot* slot = slot_of(ring);
        if (slot == nullptr) {
            return -EINVAL;
        }
        constexpr u32 known = ENTER_GETEVENTS | ENTER_ABS_TIMEOUT;
        if ((flags & ~known) != 0) {
            return -EINVAL;
        }

        astia_sq_ring* sq       = astia_rings_sq(ring);
        const u32      tail     = __atomic_load_n(&sq->tail, __ATOMIC_ACQUIRE);
        u32            head     = __atomic_load_n(&sq->head, __ATOMIC_RELAXED);
        u32            consumed = 0;
        while (consumed < to_submit && head != tail) {
            const astia_sqe s = astia_rings_sqes(ring)[head & sq->mask];
            if (!dispatch_one(*slot, ring, s)) {
                break;
            }
            head += 1;
            __atomic_store_n(&sq->head, head, __ATOMIC_RELEASE);
            consumed += 1;
        }

        if (min_complete == 0) {
            return static_cast<long>(consumed);
        }

        u64 deadline = 0;
        if (timeout_ticks != 0) {
            const u64 now = arch::timer_ticks();
            deadline = (flags & ENTER_ABS_TIMEOUT) ? timeout_ticks : now + timeout_ticks;
        }
        while (astia_cq_ready(ring) < min_complete) {
            if (!arch::cpu::check_interrupts()) {
                return -EAGAIN;
            }
            if (deadline != 0 && arch::timer_ticks() >= deadline) {
                return -ETIMEDOUT;
            }
            arch::cpu::halt();
        }
        return static_cast<long>(consumed);
    }

    long io_enter_abi(astia_rings* ring, u32 to_submit, u32 min_complete, u32 flags,
                      const astia_timespec* timeout) {
        u64 ticks = 0;
        if (timeout != nullptr) {
            const u64 hz = arch::timer_hz();
            if (hz == 0) {
                return -EIO;
            }
            if (timeout->tv_sec < 0 || timeout->tv_nsec < 0 ||
                timeout->tv_nsec >= 1000000000) {
                return -EINVAL;
            }
            // Clamp before multiplying: a caller may hand any timespec, and
            // anything beyond a year of waiting is the same as "very long".
            constexpr u64 max_ns    = 1000000000ull * 60 * 60 * 24 * 365;
            const u64     ns_wanted = static_cast<u64>(timeout->tv_sec) * 1000000000ull +
                                      static_cast<u64>(timeout->tv_nsec);
            const u64     ns        = ns_wanted > max_ns ? max_ns : ns_wanted;
            // Split so no intermediate can overflow for any sane tick rate:
            // whole seconds first, then the sub-second remainder rounded up.
            ticks = (ns / 1000000000ull) * hz +
                    ((ns % 1000000000ull) * hz + 999999999ull) / 1000000000ull;
            if (ticks == 0) {
                ticks = 1; // a non-zero request must not mean "unbounded"
            }
        }
        return io_enter(ring, to_submit, min_complete, flags, ticks);
    }

    void io_device_ready(i32 fd) {
        proc::process* current = proc::current_process();
        if (current == nullptr || !current->descriptor(fd)) {
            return;
        }
        arch::interrupt_guard gate;
        for (u32 i = 0; i < ring_count; ++i) {
            ring_slot* slot_ptr = g_slots[i];
            if (slot_ptr == nullptr || !slot_ptr->used) {
                continue;
            }
            ring_slot&   slot = *slot_ptr;
            astia_rings* ring = rings_of(slot);
            for (u32 p = 0; p < park_capacity; ++p) {
                parked_op& op = slot.park[p];
                if (op.used && op.fd == fd) {
                    retry_parked(ring, op);
                }
            }
        }
    }

    void io_device_ready_context(const io_device* dev) {
        if (dev == nullptr) {
            return;
        }
        arch::interrupt_guard gate;
        for (u32 i = 0; i < ring_count; ++i) {
            ring_slot* slot_ptr = g_slots[i];
            if (slot_ptr == nullptr || !slot_ptr->used) {
                continue;
            }
            ring_slot&   slot = *slot_ptr;
            astia_rings* ring = rings_of(slot);
            for (u32 p = 0; p < park_capacity; ++p) {
                parked_op& op = slot.park[p];
                if (op.used && op.object->backing() == dev) {
                    retry_parked(ring, op);
                }
            }
        }
    }

} // namespace astia::sys
