#include "aio.hpp"

#include "proc.hpp"
#include "user_mem.hpp"
#include "../fs/pipe.hpp"
#include <astia/api/errno.h>
#include <astia/api/termios.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/io.hpp>
#include <astia/log.hpp>
#include <astia/memory.hpp>
#include <astia/mm/iovec.hpp>

void* operator new(astia::usize size, void* where) noexcept;

/*
 * The ring engine.  Layout, cursors and ordering follow
 * <astia/api/aio.h> exactly.  The filesystem owns polymorphic file
 * instances; this layer only translates the userspace ABI's integer
 * handles to mm::shptr<fs::open_file> objects and keeps parked operations alive.
 *
 * What executes an SQE:
 *   NOP            -> completes with 0
 *   READ/WRITE     -> file::submit; a 0 moves the request into a park slot,
 *                     and the device resumes that same request (from its own
 *                     stage) when its interrupt fires
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

        // The parent keeps its ring while two forked children perform their
        // pipe handshakes.  Exiting children release their private slots.
        constexpr u32 ring_count    = 4;
        constexpr u32 park_capacity = 16;

        // Longest operation the engine will accept, in bytes.  A CQE result is
        // a positive i32 and one stretch reaches the device as a u32 length, so
        // anything longer could only be reported by clamping the count that
        // actually moved.  Linux draws the same line at MAX_RW_COUNT.
        constexpr usize max_operation_bytes = 0x7ffff000;

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

        // One in-flight operation.  The request lives here, in its final
        // address, rather than on the submitting stack: a device that parks
        // keeps this reference (and whatever it put in device_state) across
        // the park, and the engine hands the same object back on resume.
        struct parked_op {
            bool used;
            /// The device completed this operation from an interrupt
            /// handler.  The completion is posted there, but the slot is
            /// reclaimed later, in task context: releasing the pin takes
            /// the region lock and can return frames to the allocator, and
            /// an IRQ may have landed inside exactly such a critical
            /// section on this CPU.  Letting the handler take those locks
            /// would deadlock.
            bool                     done = false;
            bool                     resuming = false;
            i32                      fd;
            mm::shptr<fs::open_file> object;
            /// The caller's frames, held for the whole flight of the
            /// operation, together with the position of the operation inside
            /// them.  Empty for an in-kernel client, whose buffer is kernel
            /// memory and cannot be unmapped underneath it — its range is
            /// recorded in the same list, just with no pin behind it.
            mm::iovec  entries;
            io_request request;
            /// Bytes the operation has already moved.  One operation may span
            /// several entries and the caller hears about it once, so the
            /// completion reports this and not the last stretch's count.
            usize moved = 0;
        };

        struct ring_slot {
            bool      used;
            proc::process* owner;
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
            proc::process* caller = proc::current_process();
            for (u32 i = 0; i < ring_count; ++i) {
                // The user alias carries the ring's intra-page offset, so
                // match any address inside the slot's aliased page range.
                const u64 alias_span = static_cast<u64>(user_ring_pages) * 4096u;
                if (g_slots[i] != nullptr && g_slots[i]->used &&
                    (((caller == nullptr || caller->address_space() == nullptr) &&
                      reinterpret_cast<u64>(rings_of(*g_slots[i])) == address) ||
                     (g_slots[i]->owner == caller && g_slots[i]->user_base != 0 &&
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
            slot.owner     = nullptr;
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

        /// What one pump() did with an operation.  The two cannot share a
        /// return value: an operation that completes without moving anything
        /// (a read at end of file) carries exactly the 0 that parking uses.
        enum class io_outcome : u8 {
            parked,    ///< the node will be resumed; the operation stays in flight
            completed, ///< over, one way or another; `result` says how
        };

        /// Drive one operation through the device, one contiguous stretch at a
        /// time.  The device contract is untouched — submit() still means
        /// "> 0 moved, 0 not ready, < 0 error" and still receives one buffer —
        /// but the engine may call it once per entry, so an operation that
        /// spans several entries never forces a device to learn a cursor.
        /// A 0 that is not a park is a node finishing with nothing to move
        /// (see io_request::complete); `result` is the operation's answer
        /// once it completes — bytes moved, or -errno — and is untouched
        /// while it is parked.
        ///
        /// `op.request` must describe the stretch about to be submitted.  The
        /// resume path hands it the request the device parked on, which is why
        /// nothing here rebuilds a request before its first submit: rebuilding
        /// would wipe the device_state the parked device left behind.
        io_outcome pump(parked_op& op, long& result) {
            for (;;) {
                const usize ask = static_cast<usize>(op.request.length());
                const long  n   = op.object->submit(op.request);
                if (n == 0 && !op.request.complete()) {
                    return io_outcome::parked; // a resume picks this request up
                }
                if (n < 0) {
                    // A stretch that already moved bytes is reported as the
                    // short operation it is; an error would throw them away.
                    result = op.moved != 0 ? static_cast<long>(op.moved) : n;
                    return io_outcome::completed;
                }
                const usize consumed = op.entries.advance(static_cast<usize>(n));
                op.moved += consumed;
                if (n == 0 || consumed < ask || op.entries.done()) {
                    // Short, the way a single read() is short, the entries are
                    // spent, or the node had nothing left to give (end of
                    // file): either way the caller's operation is over.  The
                    // n == 0 arm is what stops a completed nothing from being
                    // read as a stretched entry and asked for again.
                    result = static_cast<long>(op.moved);
                    return io_outcome::completed;
                }
                // On to the next entry.  The file layer resolved the caller's
                // `offset_current` on the first submit and wrote the concrete
                // position back into the request, so continuing from the
                // request's own offset is what keeps a resume on the byte the
                // caller asked for.
                op.request.offset(op.request.offset() + consumed);
                op.request = io_request(
                    op.entries.window(), static_cast<u32>(op.entries.chunk()),
                    op.request.offset(), op.request.user_data(), op.request.direction());
            }
        }

        /// Start one operation through the device's submit() seam.  The park
        /// slot is taken up front so the request sits at its final address
        /// before the device ever sees it: handing a device a stack-local
        /// request would leave it holding a dangling pointer the instant it
        /// parked.  `entries` is the operation's whole buffer list — one range
        /// for READ/WRITE, up to max_segments of them for READV/WRITEV — and
        /// every entry is pinned for the flight.  Returns whether the SQE was
        /// consumed.
        bool submit_request(ring_slot& slot, astia_rings* ring, const astia_sqe& s,
                            const mm::shptr<fs::open_file>& object,
                            const sys::user_range* entries, u32 count,
                            io_direction direction) {
            arch::interrupt_guard gate;
            parked_op*            p = nullptr;
            for (u32 i = 0; i < park_capacity; ++i) {
                if (!slot.park[i].used) {
                    p = &slot.park[i];
                    break;
                }
            }
            if (p == nullptr) {
                // Every slot is in flight.  Fail this op rather than stall
                // the ring — same answer the pre-seam overflow gave.
                (void) cq_post(ring, s.user_data, -EBUSY);
                return true;
            }
            p->used   = true;
            p->done   = false;
            p->fd     = s.fd;
            p->object = object;
            p->moved  = 0;

            // Snapshot every entry for as long as this operation lasts.  From
            // here the device is handed kernel windows and never the user
            // pointers again, so a concurrent munmap — or a CoW split of a
            // buffer the operation is filling — cannot move or free what the
            // device is working on.
            for (u32 i = 0; i < count; ++i) {
                const long pinned =
                    sys::pin_user_range(entries[i], direction, p->entries);
                if (pinned < 0) {
                    // A failed submission keeps nothing: release whatever the
                    // earlier entries had already taken, so a refused readv()
                    // does not leave a partial pin behind.
                    p->entries.release();
                    p->object = mm::shptr<fs::open_file>{};
                    p->used   = false;
                    (void) cq_post(ring, s.user_data, static_cast<i32>(pinned));
                    return true;
                }
            }
            // Hand the device the first stretch.  The entry's window already
            // folds in the caller's byte offset inside the first page, so the
            // device works on the range the SQE named rather than on the page
            // it happens to sit on.
            p->request =
                io_request(p->entries.window(), static_cast<u32>(p->entries.chunk()),
                           s.offset, s.user_data, direction);
            long       result  = 0;
            const auto outcome = pump(*p, result);
            if (outcome == io_outcome::parked) {
                return true; // parked: a device resume completes it
            }
            // Satisfied inline.  The operation has already happened, so a
            // full CQ loses the completion rather than replaying it — the
             // CQ is sized to twice the SQ, so a wait is a bug, not a signal.
             (void) cq_post(ring, s.user_data, static_cast<i32>(result));
             p->entries.release();
             p->object = mm::shptr<fs::open_file>{};
             p->used   = false;
             return true;
        }

        /// Re-submit a parked operation: the device picks its continuation
        /// up from request.stage(), and a stretch that finishes hands the
        /// operation on to the next entry.  A device that is still not ready
        /// returns 0 and stays parked.
        void resume_parked(astia_rings* ring, parked_op& op) {
            if (op.resuming) return;
            op.resuming = true;
            long result = 0;
            if (pump(op, result) == io_outcome::parked) {
                op.resuming = false;
                return;
            }
            // A full CQ leaves the operation and its shared file
            // reference parked for the next IRQ nudge.
            if (cq_post(ring, op.request.user_data(), static_cast<i32>(result))) {
                op.done = true;
            }
            op.resuming = false;
        }

        /// Reclaim park slots whose operations finished in an interrupt
        /// handler.  Task context only: dropping a pin may take a lock and
        /// may return frames to the allocator.
        void reap_completed(ring_slot& slot) {
            arch::interrupt_guard gate;
            for (u32 i = 0; i < park_capacity; ++i) {
                parked_op& op = slot.park[i];
                if (op.used && op.done) {
                    op.entries.release();
                    op.object = mm::shptr<fs::open_file>{};
                    op.done   = false;
                    op.used   = false;
                }
            }
        }

        bool has_parked(const ring_slot& slot) {
            for (u32 i = 0; i < park_capacity; ++i) {
                if (slot.park[i].used)
                    return true;
            }
            return false;
        }

        bool dispatch_one(ring_slot& slot, astia_rings* ring, const astia_sqe& s) {
            switch (s.op) {
            case OP_NOP:
                return cq_post_guarded(ring, s.user_data, 0);

            case OP_READ:
            case OP_WRITE:
            case OP_READV:
            case OP_WRITEV: {
                mm::shptr<fs::open_file> object = file_for(s.fd);
                if (!object) {
                    return cq_post_guarded(ring, s.user_data, -EBADF);
                }
                const io_direction direction = (s.op == OP_READ || s.op == OP_READV)
                                                   ? io_direction::input
                                                   : io_direction::output;
                // READ/WRITE name one range, READV/WRITEV up to max_segments of
                // them.  Both become the same list, so the engine has one path
                // and a device still gets one buffer per submit() either way.
                sys::user_range ranges[mm::iovec::max_segments];
                u32             count = 0;
                if (s.op == OP_READ || s.op == OP_WRITE) {
                    ranges[0] = sys::user_range::bytes(s.address, s.length);
                    count     = 1;
                }
                else {
                    // For READV/WRITEV, `length` counts entries, not bytes.
                    // Bound the count before copying, so a bad one cannot ask
                    // the kernel to write past its own array.
                    if (s.length > mm::iovec::max_segments) {
                        return cq_post_guarded(ring, s.user_data, -EINVAL);
                    }
                    astia_iovec raw[mm::iovec::max_segments];
                    if (s.length != 0 &&
                        !sys::copy_in(raw, sys::user_range::bytes(
                                               s.address, static_cast<usize>(s.length) *
                                                              sizeof(astia_iovec)))) {
                        return cq_post_guarded(ring, s.user_data, -EFAULT);
                    }
                    // The ABI spells each entry's address as a pointer; decode
                    // it into the kernel's own range type here, at the one line
                    // that reads the caller's array, so nothing below this sees
                    // a raw pointer out of user memory.
                    for (u64 i = 0; i < s.length; ++i) {
                        ranges[i] =
                            sys::user_range::bytes(reinterpret_cast<u64>(raw[i].base),
                                                   static_cast<usize>(raw[i].len));
                    }
                    count = static_cast<u32>(s.length);
                }
                // Validated at submission, before anything is pinned, so a bad
                // list is refused whole; the ranges are then held for the
                // flight of the operation (see submit_request), so another
                // thread of the process unmapping one cannot pull a buffer out
                // from under the device.
                usize total = 0;
                for (u32 i = 0; i < count; ++i) {
                    if (!sys::user_range_ok(ranges[i])) {
                        return cq_post_guarded(ring, s.user_data, -EFAULT);
                    }
                    if (ranges[i].length() > max_operation_bytes ||
                        total > max_operation_bytes - ranges[i].length()) {
                        return cq_post_guarded(ring, s.user_data, -EINVAL);
                    }
                    total += ranges[i].length();
                }
                // Zero bytes is satisfied, not parked: 0 is submit()'s "in
                // flight", so a zero-length op must never reach it — a
                // parked zero-length read would wait for a wake that can
                // never come.
                if (total == 0) {
                    return cq_post_guarded(ring, s.user_data, 0);
                }
                return submit_request(slot, ring, s, object, ranges, count, direction);
            }

            case OP_OPENAT: {
                // The pathname is user memory: copy it into the kernel
                // rather than letting the VFS walk a raw user pointer.
                char path[256];
                if (!sys::copy_in_string(path, sizeof path,
                                         sys::user_range::string(s.address))) {
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
                const sys::user_range out =
                    sys::user_range::bytes(s.offset, sizeof(astia_stat));
                if (!sys::user_range_ok(out)) {
                    return cq_post_guarded(ring, s.user_data, -EFAULT);
                }
                if (s.length != sizeof(astia_stat)) {
                    return cq_post_guarded(ring, s.user_data, -EINVAL);
                }

                mm::shptr<fs::open_file> object;
                if (s.address != 0) {
                    char path[256];
                    if (!sys::copy_in_string(path, sizeof path,
                                             sys::user_range::string(s.address))) {
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
                // mode/uid/times stay zero rather than being guessed.  The
                // range was validated above; this is the write it licensed.
                auto* stat_out = reinterpret_cast<astia_stat*>(out.address());
                __builtin_memset(stat_out, 0, sizeof *stat_out);
                stat_out->size   = static_cast<u64>(size);
                stat_out->blocks = (static_cast<u64>(size) + 511u) / 512u;
                return cq_post_guarded(ring, s.user_data, 0);
            }

            case OP_READDIR: {
                char path[256];
                if (!sys::copy_in_string(path, sizeof path,
                                         sys::user_range::string(s.offset)))
                    return cq_post_guarded(ring, s.user_data, -EFAULT);
                const sys::user_range out =
                    sys::user_range::bytes(s.address, sizeof(astia_dirent));
                if (!sys::user_range_ok(out))
                    return cq_post_guarded(ring, s.user_data, -EFAULT);
                auto* result = reinterpret_cast<astia_dirent*>(out.address());
                __builtin_memset(result, 0, sizeof *result);
                long rc = fs::read_directory(path, s.fd < 0 ? 0 : s.fd,
                                              result->name, sizeof result->name,
                                              &result->type);
                if (rc > 0) {
                    u32 length = 0;
                    while (length < sizeof result->name && result->name[length] != '\0')
                        ++length;
                    result->namelen = length;
                }
                return cq_post_guarded(ring, s.user_data, static_cast<i32>(rc));
            }

            case OP_IOCTL: {
                /* ioctl is a control-plane fence.  Do not consume this SQE
                   while an older read/write is parked: otherwise a batch
                   containing READ, IOCTL, READ could observe tty state out
                   of order.  Returning false leaves SQ head at IOCTL; the
                   wake-up path retries it after the parked operation posts. */
                if (has_parked(slot))
                    return false;
                if (s.length == 0 || !sys::user_range_ok(
                        sys::user_range::bytes(s.address, s.length)))
                    return cq_post_guarded(ring, s.user_data, -EFAULT);
                mm::shptr<fs::open_file> object = file_for(s.fd);
                if (!object)
                    return cq_post_guarded(ring, s.user_data, -EBADF);
                __atomic_thread_fence(__ATOMIC_SEQ_CST);
                long rc = object->ioctl(s.offset,
                                        reinterpret_cast<void*>(s.address));
                __atomic_thread_fence(__ATOMIC_SEQ_CST);
                return cq_post_guarded(ring, s.user_data, static_cast<i32>(rc));
            }

            case OP_PIPE: {
                const sys::user_range out = sys::user_range::bytes(s.address, 2 * sizeof(i32));
                if (!sys::user_range_ok(out))
                    return cq_post_guarded(ring, s.user_data, -EFAULT);
                proc::process* current = proc::current_process();
                if (current == nullptr)
                    return cq_post_guarded(ring, s.user_data, -ESRCH);
                mm::shptr<fs::open_file> read_end;
                mm::shptr<fs::open_file> write_end;
                long rc = fs::create_pipe(read_end, write_end);
                if (rc < 0)
                    return cq_post_guarded(ring, s.user_data, static_cast<i32>(rc));
                i32 read_fd = -1;
                i32 write_fd = -1;
                for (i32 fd = 3; fd < static_cast<i32>(proc::max_process_fds); ++fd) {
                    if (read_fd < 0 && !current->descriptor(fd)) {
                        current->set_descriptor(fd, read_end);
                        read_fd = fd;
                    }
                    else if (write_fd < 0 && !current->descriptor(fd)) {
                        current->set_descriptor(fd, write_end);
                        write_fd = fd;
                        break;
                    }
                }
                if (read_fd < 0 || write_fd < 0) {
                    if (read_fd >= 0) current->clear_descriptor(read_fd);
                    return cq_post_guarded(ring, s.user_data, -EMFILE);
                }
                auto* result = reinterpret_cast<i32*>(out.address());
                result[0] = read_fd;
                result[1] = write_fd;
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
            slot.owner = proc::current_process();

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
        // Reclaim everything the device already finished, then refuse only
        // while an operation is genuinely still in flight.
        reap_completed(*slot);
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
        // An interrupted completion is only a posted CQE until someone in
        // task context says otherwise; do that before claiming capacity.
        reap_completed(*slot);

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
                if (op.used && !op.done && op.fd == fd) {
                    resume_parked(ring, op);
                }
            }
        }
    }

    void io_device_ready_context(const io_device* dev) {
        io_device_ready_context_except(dev, ~u64{0});
    }

    void io_device_ready_context_except(const io_device* dev, u64 excluded_user_data) {
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
                if (op.used && !op.done && op.object &&
                    op.request.user_data() != excluded_user_data &&
                    op.object->backing() == dev) {
                    resume_parked(ring, op);
                }
            }
        }
    }

    void io_exec_process(proc::process* owner) {
        if (owner == nullptr) return;
        for (u32 i = 0; i < ring_count; ++i) {
            ring_slot* slot = g_slots[i];
            if (slot == nullptr || !slot->used || slot->owner != owner)
                continue;
            reap_completed(*slot);
            unmap_ring_user(*slot, owner);
            clear_slot(*slot);
        }
    }

    void io_exit_process(proc::process* owner) {
        if (owner == nullptr) return;
        for (u32 i = 0; i < ring_count; ++i) {
            ring_slot* slot = g_slots[i];
            if (slot == nullptr || !slot->used || slot->owner != owner)
                continue;
            reap_completed(*slot);
            bool busy = false;
            for (u32 p = 0; p < park_capacity; ++p) {
                if (slot->park[p].used) {
                    busy = true;
                    break;
                }
            }
            if (busy)
                continue;
            unmap_ring_user(*slot, owner);
            clear_slot(*slot);
            g_slots[i] = nullptr;
            slot->~ring_slot();
        }
    }

} // namespace astia::sys
