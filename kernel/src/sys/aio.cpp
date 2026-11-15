#include "aio.hpp"
#include "aio_ring.hpp"

#include "../fs/mapping.hpp"
#include "../ipc/pipe.hpp"
#include "../ipc/unix_socket.hpp"
#include "../proc/mapping.hpp"
#include "proc.hpp"
#include "time.hpp"
#include "user_mem.hpp"
#include <astia/api/errno.h>
#include <astia/api/termios.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/arch/platform.hpp>
#include <astia/io.hpp>
#include <astia/log.hpp>
#include <astia/memory.hpp>
#include <astia/mm/iovec.hpp>
#include <astia/mm/share.hpp>

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

    using namespace aio_ring;

    namespace {
        class eventfd_file final : public fs::file {
            public:
            eventfd_file(u64 value, bool nonblock) : value_(value), nonblock_(nonblock) {}

            fs::file_mode mode() const override {
                return fs::file_mode(fs::file_type::character, fs::default_device_permissions);
            }

            long read(void* buffer, u32 length, u64) override {
                if (buffer == nullptr || length != sizeof(u64))
                    return -EINVAL;
                u64 value = __atomic_exchange_n(&value_, 0, __ATOMIC_ACQ_REL);
                if (value == 0)
                    return 0;
                *static_cast<u64*>(buffer) = value;
                sys::io_device_ready_context(this);
                return sizeof(u64);
            }

            long write(const void* buffer, u32 length, u64) override {
                if (buffer == nullptr || length != sizeof(u64))
                    return -EINVAL;
                u64 add = *static_cast<const u64*>(buffer);
                if (add == UINT64_MAX)
                    return -EINVAL;
                u64 old = __atomic_load_n(&value_, __ATOMIC_RELAXED);
                for (;;) {
                    if (UINT64_MAX - old < add)
                        return nonblock_ ? -EAGAIN : 0;
                    if (__atomic_compare_exchange_n(&value_, &old, old + add, false,
                                                    __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
                        sys::io_device_ready_context(this);
                        return sizeof(u64);
                    }
                }
            }

            u32 poll(u32 events, u32) override {
                u32 ready = 0;
                if (__atomic_load_n(&value_, __ATOMIC_ACQUIRE) != 0)
                    ready |= POLLIN;
                if (__atomic_load_n(&value_, __ATOMIC_ACQUIRE) != UINT64_MAX)
                    ready |= POLLOUT;
                return ready & events;
            }

            long submit(io_request& request) override {
                if (request.length() != sizeof(u64))
                    return -EINVAL;
                const long result = request.direction() == io_direction::input
                                        ? read(request.buffer(), request.length(), 0)
                                        : write(request.buffer(), request.length(), 0);
                if (result == 0 && nonblock_)
                    return -EAGAIN;
                if (result == 0)
                    request.stage(1);
                return result;
            }

            private:
            u64  value_;
            bool nonblock_;
        };

        long resolve_at(proc::process* current, i32 raw_dirfd, const char* path,
                        char* out, usize capacity) {
            if (current == nullptr)
                return -ESRCH;
            // The legacy path-only libc wrappers encode no directory fd as
            // -1; treat it like AT_FDCWD just as the explicit *at wrappers
            // do for AT_FDCWD.
            if (raw_dirfd == AT_FDCWD || raw_dirfd == -1)
                return current->compose_path_at(fd_t{AT_FDCWD}, path, out, capacity);
            auto dirfd = mapper<fd_t>::from(raw_dirfd);
            if (!dirfd.has_value)
                return -EBADF;
            return current->compose_path_at(dirfd.value, path, out, capacity);
        }

        bool parent_writable(proc::process* current, const char* path) {
            if (current == nullptr || path == nullptr)
                return false;
            char parent[256]{};
            u32  n = 0;
            while (path[n])
                ++n;
            while (n > 1 && path[n - 1] != '/')
                --n;
            for (u32 i = 0; i < (n > 1 ? n - 1 : 1); ++i)
                parent[i] = path[i];
            parent[n > 1 ? n - 1 : 1] = '\0';
            const fs::file_mode mode  = fs::node_mode(parent);
            if (!mode.is_directory())
                return false;
            return current->can_access(mode, fs::node_uid(parent), fs::node_gid(parent),
                                       fs::permission_bits::write) &&
                   current->can_access(mode, fs::node_uid(parent), fs::node_gid(parent),
                                       fs::permission_bits::execute);
        }

        bool socket_node_writable(proc::process* current, const char* path) {
            if (!current || !path)
                return false;
            const fs::file_type type = fs::node_type(path);
            // Leave absence and non-socket paths to the endpoint operation so
            // connect/sendto preserve their ECONNREFUSED result.
            if (type == fs::directory_unknown)
                return true;
            if (type != fs::directory_sock)
                return true;
            return current->can_access(fs::node_mode(path), fs::node_uid(path),
                                       fs::node_gid(path), fs::permission_bits::write);
        }

        constexpr usize max_operation_bytes = 0x7ffff000;

        u64 poll_now_ns() {
            const u64 hz = arch::timer_hz();
            if (hz == 0)
                return 0;
            const u64 ticks    = arch::timer_ticks();
            const u64 seconds  = ticks / hz;
            const u64 fraction = (ticks % hz) * nanoseconds_per_second / hz;
            if (seconds > UINT64_MAX / nanoseconds_per_second)
                return UINT64_MAX;
            const u64 whole = seconds * nanoseconds_per_second;
            return whole > UINT64_MAX - fraction ? UINT64_MAX : whole + fraction;
        }

        void arm_request_timeout(parked_op& op) {
            op.request.timeout_expired(false);
            if (!op.request.timeout_set() || op.request.timeout_ns() == 0) {
                op.timed = false;
                return;
            }
            const u64 now = poll_now_ns();
            op.timed = true;
            op.timeout_deadline = UINT64_MAX - now < op.request.timeout_ns()
                                      ? UINT64_MAX
                                      : now + op.request.timeout_ns();
        }

        void complete_control(proc::process* owner, astia_rings* ring, parked_op& op,
                              i32 result) {
            if (op.done || op.pending_cqe)
                return;
            if (result == -EINTR && op.sleeping && !op.sleep_absolute &&
                op.sleep_remaining_user != 0) {
                const u64 now       = poll_now_ns();
                const u64 remaining = now >= op.sleep_deadline ? 0 : op.sleep_deadline - now;
                if (!sys::copy_out_for(owner,
                                       span<u8>::bytes(op.sleep_remaining_user, sizeof remaining),
                                       &remaining))
                    result = -EFAULT;
            }
            if (op.request.completion() != nullptr) {
                if (result < 0)
                    op.request.completion()->fail_deferred(error_from_errno(-result));
                else
                    op.request.completion()->complete_deferred(static_cast<i64>(result));
                op.done = true;
            }
            else if (!cq_post(ring, op.request.user_data(), static_cast<i32>(result))) {
                op.pending_cqe       = true;
                op.pending_user_data = op.request.user_data();
                op.pending_result    = static_cast<i32>(result);
            }
            else
                op.done = true;
        }

        mm::shptr<fs::open_file> file_for(proc::process* current, i32 fd) {
            auto mapped = mapper<fd_t>::from(fd);
            if (!mapped.has_value)
                return {};
            return current == nullptr ? mm::shptr<fs::open_file>{}
                                      : current->descriptor(mapped.value);
        }

        mm::shptr<fs::open_file> file_for(i32 fd) {
            return file_for(proc::current_process(), fd);
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
                long        n   = 0;
                if (op.socket_accept) {
                    const auto* socket = op.object->node()->unix_socket_capability();
                    n = socket == nullptr ? -EOPNOTSUPP
                                          : socket->accept(*op.object, op.request);
                }
                else {
                    n = op.object->submit(op.request);
                }
                if (op.control) {
                    if (n == 0 && !op.request.complete())
                        return io_outcome::parked;
                    result = n;
                    return io_outcome::completed;
                }
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
                const bool updates_position = op.request.updates_position();
                const bool append_reserved = op.request.append_reserved();
                const bool has_append_length = op.request.has_append_length();
                const u32 append_length = op.request.append_length();
                op.request = io_request(
                    op.entries.window(), static_cast<u32>(op.entries.chunk()),
                    op.request.offset(), op.request.user_data(), op.request.direction());
                op.request.updates_position(updates_position);
                if (has_append_length)
                    op.request.append_length(append_length);
                op.request.append_reserved(append_reserved);
            }
        }

        void complete_recvfrom_address(parked_op& op, long& result) {
            if (op.recvfrom_address && result >= 0) {
                astia_sockaddr address{};
                const auto*    socket = op.object->node()->unix_socket_capability();
                const auto     destination =
                    sys::user_ptr<astia_sockaddr>(op.recvfrom_address_user).validate();
                if (socket == nullptr || !socket->recvfrom_address(*op.object, address) ||
                    !destination || !sys::copy_out(destination.value.bytes(), &address))
                    result = -EFAULT;
            }
        }

        void resume_parked(astia_rings* ring, parked_op& op);

        bool publish_pending(astia_rings* ring, parked_op& op) {
            if (!op.pending_cqe)
                return true;
            if (!cq_post(ring, op.pending_user_data, op.pending_result))
                return false;
            op.pending_cqe = false;
            op.done        = true;
            return true;
        }

        void retry_pending(ring_slot& slot, astia_rings* ring) {
            for (u32 i = 0; i < parked_operation_slots; ++i) {
                parked_op& op = slot.park[i];
                if (op.used && op.pending_cqe && !publish_pending(ring, op))
                    break;
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
                            const span<u8>* entries, u32 count, io_direction direction,
                            bool control = false, bool socket_accept = false) {
            parked_op* p          = nullptr;
            u32        park_index = 0;
            for (u32 i = 0; i < parked_operation_slots; ++i) {
                if (!slot.park[i].used) {
                    p          = &slot.park[i];
                    park_index = i;
                    break;
                }
            }
            if (p == nullptr) {
                // Every slot is in flight.  Fail this op rather than stall
                // the ring — same answer the pre-seam overflow gave.
                return cq_post(ring, s.user_data, -EBUSY);
            }
            p->used                  = true;
            p->done                  = false;
            p->resuming              = false;
            p->fd                    = s.fd;
            p->object                = object;
            p->moved                 = 0;
            p->control               = control;
            p->signal_interruptible  = false;
            p->socket_accept         = socket_accept;
            p->readiness             = false;
            p->poll_events           = 0;
            p->pending_cqe           = false;
            p->pending_user_data     = 0;
            p->pending_result        = 0;
            p->recvfrom_address      = s.op == OP_RECVFROM;
            p->recvfrom_address_user = p->recvfrom_address ? s.offset : 0;

            // Snapshot every entry for as long as this operation lasts.  From
            // here the device is handed kernel windows and never the user
            // pointers again, so a concurrent munmap — or a CoW split of a
            // buffer the operation is filling — cannot move or free what the
            // device is working on.
            for (u32 i = 0; i < count; ++i) {
                const long pinned = sys::pin_user_span(entries[i], direction, p->entries);
                if (pinned < 0) {
                    // A failed submission keeps nothing: release whatever the
                    // earlier entries had already taken, so a refused readv()
                    // does not leave a partial pin behind.
                    // Leave the resources in the slot until task context
                    // reaps it below; pin/file release must not run under the
                    // ring lock, even for a rejected submission.
                    if (!cq_post(ring, s.user_data, static_cast<i32>(pinned))) {
                        p->pending_cqe       = true;
                        p->pending_user_data = s.user_data;
                        p->pending_result    = static_cast<i32>(pinned);
                    }
                    else
                        p->done = true;
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
            p->request.append_length(static_cast<u32>(p->entries.bytes()));
            p->request.updates_position((s.op_flags & ASTIA_AIO_POSITIONED) == 0);
            // Capture before submit(): if readiness is reported while the
            // device is testing the request, publication below must notice it.
            const u64  generation = ready_generation();
            long       result     = 0;
            const auto outcome    = pump(*p, result);
            if (outcome == io_outcome::parked) {
                arm_request_timeout(*p);
                publish_waiter_hint(slot, park_index, p->object->node().get());
                if (ready_generation() != generation)
                    resume_parked(ring, *p);
                return true; // parked: a device resume completes it
            }
            complete_recvfrom_address(*p, result);
            // Satisfied inline.  The operation has already happened, so a
            // full CQ loses the completion rather than replaying it — the
            // CQ is sized to twice the SQ, so a wait is a bug, not a signal.
            if (p->request.completion() != nullptr) {
                if (result < 0)
                    p->request.completion()->fail_deferred(error_from_errno(-result));
                else
                    p->request.completion()->complete_deferred(static_cast<i64>(result));
            }
            else
                if (!cq_post(ring, s.user_data, static_cast<i32>(result))) {
                    p->pending_cqe       = true;
                    p->pending_user_data = s.user_data;
                    p->pending_result    = static_cast<i32>(result);
                    return true;
                }
            // Reap after the ring lock is released; this operation uses the
            // same park storage so its pins and open-file reference follow
            // the parked cleanup rule too.
            p->done = true;
            return true;
        }

        /// Re-submit a parked operation: the device picks its continuation
        /// up from request.stage(), and a stretch that finishes hands the
        /// operation on to the next entry.  A device that is still not ready
        /// returns 0 and stays parked.
        void resume_parked(astia_rings* ring, parked_op& op) {
            // Cancellation posts the target CQE and marks the operation done,
            // but the readiness nudge that was already in flight may still
            // reach this slot before task context reaps it.  Never post a
            // second completion for a canceled operation.
            if (op.resuming || op.done)
                return;
            op.resuming = true;
            if (op.pending_cqe) {
                (void) publish_pending(ring, op);
                op.resuming = false;
                return;
            }
            if (op.readiness) {
                const u32 ready = op.object ? op.object->poll(op.poll_events)
                                            : static_cast<u32>(POLLNVAL);
                if (ready == 0) {
                    op.resuming = false;
                    return;
                }
                if (op.request.completion() != nullptr) {
                    op.request.completion()->complete_deferred(static_cast<i64>(ready));
                    op.done = true;
                }
                else if (!cq_post(ring, op.request.user_data(), static_cast<i32>(ready))) {
                    op.pending_cqe       = true;
                    op.pending_user_data = op.request.user_data();
                    op.pending_result    = static_cast<i32>(ready);
                }
                else
                    op.done = true;
                op.resuming = false;
                return;
            }
            long result = 0;
            if (pump(op, result) == io_outcome::parked) {
                arm_request_timeout(op);
                op.resuming = false;
                return;
            }
            complete_recvfrom_address(op, result);
            // A full CQ leaves the operation and its shared file
            // reference parked for the next IRQ nudge.
            if (op.request.completion() != nullptr) {
                if (result < 0)
                    op.request.completion()->fail_deferred(error_from_errno(-result));
                else
                    op.request.completion()->complete_deferred(static_cast<i64>(result));
                op.done = true;
            }
            else if (!cq_post(ring, op.request.user_data(), static_cast<i32>(result))) {
                op.pending_cqe       = true;
                op.pending_user_data = op.request.user_data();
                op.pending_result    = static_cast<i32>(result);
            }
            else
                op.done = true;
            op.resuming = false;
        }

        bool has_parked(const ring_slot& slot) {
            for (u32 i = 0; i < parked_operation_slots; ++i) {
                if (slot.park[i].used && !slot.park[i].done)
                    return true;
            }
            return false;
        }

        struct dispatch_context {
            ring_slot&       slot;
            astia_rings*     ring;
            const astia_sqe& sqe;
            proc::process*   current;

            bool complete(i32 result) const {
                return cq_post_guarded(ring, sqe.user_data, result);
            }
        };

        bool dispatch_io(dispatch_context& context) {
            const astia_sqe&         s      = context.sqe;
            mm::shptr<fs::open_file> object = file_for(context.current, s.fd);
            if (!object) {
                return context.complete(-EBADF);
            }
            const io_direction direction =
                (s.op == OP_READ || s.op == OP_RECV || s.op == OP_READV)
                    ? io_direction::input
                    : io_direction::output;
            const u32 access = object->flags() & fs::open_file_access_mask;
            if ((direction == io_direction::output && access == 0 &&
                 (object->flags() & fs::pipe_write_endpoint_flag) == 0 &&
                 object->mode().is_regular()) ||
                (direction == io_direction::input && access == ASTIA_AIO_O_WRONLY))
                return context.complete(-EBADF);
            // READ/WRITE name one range, READV/WRITEV up to max_segments of
            // them.  Both become the same list, so the engine has one path
            // and a device still gets one buffer per submit() either way.
            span<u8> ranges[mm::iovec::max_segments];
            u32      count = 0;
            if (s.op == OP_READ || s.op == OP_WRITE || s.op == OP_SEND ||
                s.op == OP_RECV) {
                ranges[0] = span<u8>::bytes(s.address, s.length);
                count     = 1;
            }
            else {
                // For READV/WRITEV, `length` counts entries, not bytes.
                // Bound the count before copying, so a bad one cannot ask
                // the kernel to write past its own array.
                if (s.length > mm::iovec::max_segments) {
                    return context.complete(-EINVAL);
                }
                astia_iovec raw[mm::iovec::max_segments];
                if (s.length != 0 &&
                    !sys::copy_in(
                        raw, span<u8>::bytes(s.address, static_cast<usize>(s.length) *
                                                            sizeof(astia_iovec)))) {
                    return context.complete(-EFAULT);
                }
                // The ABI spells each entry's address as a pointer; decode
                // it into the kernel's own range type here, at the one line
                // that reads the caller's array, so nothing below this sees
                // a raw pointer out of user memory.
                for (u64 i = 0; i < s.length; ++i) {
                    ranges[i] = span<u8>::bytes(reinterpret_cast<u64>(raw[i].base),
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
                if (!sys::user_span_ok(ranges[i])) {
                    return context.complete(-EFAULT);
                }
                if (ranges[i].length() > max_operation_bytes ||
                    total > max_operation_bytes - ranges[i].length()) {
                    return context.complete(-EINVAL);
                }
                total += ranges[i].length();
            }
            // Zero bytes is satisfied, not parked: 0 is submit()'s "in
            // flight", so a zero-length op must never reach it — a
            // parked zero-length read would wait for a wake that can
            // never come.
            if (total == 0) {
                return context.complete(0);
            }
            return submit_request(context.slot, context.ring, s, object, ranges, count,
                                  direction);
        }

        bool handle_openat(astia_rings* ring, const astia_sqe& s) {
            // The pathname is user memory: copy it into the kernel rather
            // than letting the VFS walk a raw user pointer.
            char path[256];
            if (!sys::copy_in_string(span<char>(path, sizeof path),
                                     sys::user_ptr<char>(s.address)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            auto* current = proc::current_process();
            char  resolved[256];
            if (current == nullptr)
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            long resolve_rc = resolve_at(current, s.fd == -1 ? AT_FDCWD : s.fd, path,
                                         resolved, sizeof resolved);
            if (resolve_rc >= 0)
                resolve_rc = fs::resolve_path(resolved, resolved, sizeof resolved, true);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
            if (resolved[0] == '/' && resolved[1] == 'd' && resolved[2] == 'e' &&
                resolved[3] == 'v' && resolved[4] == '/' && resolved[5] == 't' &&
                resolved[6] == 't' && resolved[7] == 'y' && resolved[8] == '\0' &&
                !proc::has_controlling_terminal())
                return cq_post_guarded(ring, s.user_data, -ENXIO);
            const fs::file_mode existing_mode = fs::node_mode(resolved);
            if (existing_mode.exists() && existing_mode.is_directory() &&
                (s.op_flags & ASTIA_AIO_O_DIRECTORY) == 0)
                return cq_post_guarded(ring, s.user_data, -EISDIR);
            if (existing_mode.exists() && !existing_mode.is_directory() &&
                (s.op_flags & ASTIA_AIO_O_DIRECTORY) != 0)
                return cq_post_guarded(ring, s.user_data, -ENOTDIR);
            if (!existing_mode.exists() && (s.op_flags & ASTIA_AIO_O_CREAT) == 0)
                return cq_post_guarded(ring, s.user_data, -ENOENT);
            if (existing_mode.exists()) {
                auto*       cred  = proc::current_process();
                const uid_t owner = fs::node_uid(resolved);
                const gid_t group = fs::node_gid(resolved);
                if ((s.op_flags & ASTIA_AIO_O_RDWR) != 0 &&
                    !cred->can_access(existing_mode, owner, group,
                                      fs::permission_bits::write))
                    return cq_post_guarded(ring, s.user_data, -EACCES);
                if ((s.op_flags & ASTIA_AIO_O_WRONLY) == 0 &&
                    !cred->can_access(existing_mode, owner, group,
                                      fs::permission_bits::read))
                    return cq_post_guarded(ring, s.user_data, -EACCES);
            }
            mm::shptr<fs::open_file> object = existing_mode.is_directory()
                                                  ? fs::open_directory(resolved)
                                                  : fs::open(resolved, s.op_flags);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -ENOENT);
            if ((s.op_flags & ASTIA_AIO_O_CREAT) != 0 && !existing_mode.exists()) {
                auto requested = mapper<fs::file_permissions>::from(
                    s.aux == 0 ? fs::default_file_permissions.raw() : s.aux);
                if (!requested.has_value)
                    return cq_post_guarded(ring, s.user_data, -EINVAL);
                (void) fs::chmod_path(resolved, requested.value);
                (void) fs::chown_path(resolved, uid_t{current->uid()},
                                      gid_t{current->gid()});
            }
            current = proc::current_process();
            if (current == nullptr)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            for (i32 i = 0; i < static_cast<i32>(proc::max_process_fds); ++i) {
                if (!current->descriptor(fd_t{i}) &&
                    current->set_descriptor(fd_t{i}, object,
                                            (s.op_flags & ASTIA_AIO_O_CLOEXEC) != 0
                                                ? static_cast<u32>(ASTIA_FD_CLOEXEC)
                                                : 0u))
                    return cq_post_guarded(ring, s.user_data, i);
            }
            return cq_post_guarded(ring, s.user_data, -EMFILE);
        }

        bool handle_mkdirat(ring_slot&, astia_rings* ring, const astia_sqe& s) {
            char path[256];
            if (!sys::copy_in_string(span<char>(path, sizeof path),
                                     sys::user_ptr<char>(s.address)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            auto* current = proc::current_process();
            char  resolved[256];
            if (current == nullptr)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            long resolve_rc = resolve_at(current, s.fd, path, resolved, sizeof resolved);
            if (resolve_rc >= 0)
                resolve_rc = fs::resolve_path(resolved, resolved, sizeof resolved, true);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
            auto requested = mapper<fs::file_mode>::from(
                s.aux == 0 ? fs::default_directory_permissions.raw() : s.aux);
            if (!requested.has_value)
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            const long rc = fs::make_directory(resolved, requested.value);
            if (rc >= 0)
                (void) fs::chown_path(resolved, uid_t{current->uid()},
                                      gid_t{current->gid()});
            return cq_post_guarded(ring, s.user_data, static_cast<i32>(rc));
        }

        bool handle_unlinkat(ring_slot&, astia_rings* ring, const astia_sqe& s) {
            char path[256];
            if (!sys::copy_in_string(span<char>(path, sizeof path),
                                     sys::user_ptr<char>(s.address)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            auto* current = proc::current_process();
            char  resolved[256];
            if (current == nullptr)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            long resolve_rc = resolve_at(current, s.fd, path, resolved, sizeof resolved);
            if (resolve_rc >= 0)
                resolve_rc = fs::resolve_path(resolved, resolved, sizeof resolved, false);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
            const long rc =
                fs::remove_path(resolved, (s.op_flags & UNLINKAT_REMOVEDIR) != 0);
            return cq_post_guarded(ring, s.user_data, static_cast<i32>(rc));
        }

        bool handle_renameat(ring_slot&, astia_rings* ring, const astia_sqe& s) {
            char old_path[256], new_path[256];
            if (!sys::copy_in_string(span<char>(old_path, sizeof old_path),
                                     sys::user_ptr<char>(s.address)) ||
                !sys::copy_in_string(span<char>(new_path, sizeof new_path),
                                     sys::user_ptr<char>(s.offset)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            auto* current = proc::current_process();
            char  old_resolved[256], new_resolved[256];
            if (current == nullptr)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            long old_rc =
                resolve_at(current, s.fd, old_path, old_resolved, sizeof old_resolved);
            i32  new_dirfd = s.length == 0 ? AT_FDCWD : static_cast<i32>(s.length);
            long new_rc    = resolve_at(current, new_dirfd, new_path, new_resolved,
                                        sizeof new_resolved);
            if (old_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) old_rc);
            if (new_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) new_rc);
            if (!parent_writable(current, old_resolved) ||
                !parent_writable(current, new_resolved))
                return cq_post_guarded(ring, s.user_data, -EACCES);
            return cq_post_guarded(
                ring, s.user_data,
                static_cast<i32>(fs::rename_path(old_resolved, new_resolved)));
        }

        bool handle_linkat(ring_slot&, astia_rings* ring, const astia_sqe& s) {
            char old_path[256], new_path[256];
            if (!sys::copy_in_string(span<char>(old_path, sizeof old_path),
                                     sys::user_ptr<char>(s.address)) ||
                !sys::copy_in_string(span<char>(new_path, sizeof new_path),
                                     sys::user_ptr<char>(s.offset)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            auto* current = proc::current_process();
            if (current == nullptr)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            char old_resolved[256], new_resolved[256];
            long old_rc =
                resolve_at(current, s.fd, old_path, old_resolved, sizeof old_resolved);
            i32  new_dirfd = s.length == 0 ? AT_FDCWD : static_cast<i32>(s.length);
            long new_rc    = resolve_at(current, new_dirfd, new_path, new_resolved,
                                        sizeof new_resolved);
            if (old_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) old_rc);
            if (new_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) new_rc);
            old_rc =
                fs::resolve_path(old_resolved, old_resolved, sizeof old_resolved, true);
            new_rc =
                fs::resolve_path(new_resolved, new_resolved, sizeof new_resolved, false);
            if (old_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) old_rc);
            if (new_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) new_rc);
            if (!parent_writable(current, old_resolved) ||
                !parent_writable(current, new_resolved))
                return cq_post_guarded(ring, s.user_data, -EACCES);
            return cq_post_guarded(
                ring, s.user_data,
                static_cast<i32>(fs::link_path(old_resolved, new_resolved)));
        }

        bool handle_statx(astia_rings* ring, const astia_sqe& s) {
            // Footnote ‡ of <astia/api/aio.h>: address NULL means fd is
            // the stat target (fstat), otherwise address is a pathname
            // and fd is the dirfd. The VFS still has one exact-path
            // namespace, so the dirfd is accepted and ignored.
            const auto out = sys::user_ptr<astia_stat>(s.offset).validate();
            if (!out) {
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            }
            if (!sys::user_span_writable_ok_for(
                    proc::current_process(), span<u8>::bytes(s.offset, sizeof(astia_stat)))) {
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            }
            if (s.length != sizeof(astia_stat)) {
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            }

            mm::shptr<fs::open_file> object;
            if (s.address != 0) {
                char path[256];
                if (!sys::copy_in_string(span<char>(path, sizeof path),
                                         sys::user_ptr<char>(s.address))) {
                    return cq_post_guarded(ring, s.user_data, -EFAULT);
                }
                auto* current = proc::current_process();
                char  resolved[256];
                if (current == nullptr)
                    return cq_post_guarded(ring, s.user_data, -EINVAL);
                long resolve_rc = resolve_at(current, s.fd == -1 ? AT_FDCWD : s.fd, path,
                                             resolved, sizeof resolved);
                if (resolve_rc >= 0)
                    resolve_rc = fs::resolve_path(resolved, resolved, sizeof resolved,
                                                  (s.op_flags & STATX_NOFOLLOW) == 0);
                if (resolve_rc < 0)
                    return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
                const fs::file_type type = fs::node_type(resolved);
                if ((s.op_flags & STATX_NOFOLLOW) != 0 && type == fs::directory_lnk) {
                    __builtin_memset(reinterpret_cast<void*>(out.value.get()), 0,
                                     sizeof(astia_stat));
                    auto* link = reinterpret_cast<astia_stat*>(out.value.get());
                    char  target[256]{};
                    long  n = fs::read_symlink(resolved, target, sizeof target);
                    if (n < 0)
                        return cq_post_guarded(ring, s.user_data, static_cast<i32>(n));
                    link->dev   = 1;
                    link->ino   = 1;
                    link->nlink = 1;
                    link->size  = (u64) n;
                    link->mode  = mapper<fs::file_mode>::to(fs::node_mode(resolved));
                    link->uid   = static_cast<u32>(fs::node_uid(resolved));
                    link->gid   = static_cast<u32>(fs::node_gid(resolved));
                    return cq_post_guarded(ring, s.user_data, 0);
                }
                object = fs::open_path(resolved);
                if (type == fs::directory_dir) {
                    __builtin_memset(reinterpret_cast<void*>(out.value.get()), 0,
                                     sizeof(astia_stat));
                    auto* directory  = reinterpret_cast<astia_stat*>(out.value.get());
                    directory->dev   = 1;
                    directory->ino   = 1;
                    directory->nlink = 1;
                    directory->mode  = mapper<fs::file_mode>::to(fs::node_mode(resolved));
                    directory->uid   = static_cast<u32>(fs::node_uid(resolved));
                    directory->gid   = static_cast<u32>(fs::node_gid(resolved));
                    return cq_post_guarded(ring, s.user_data, 0);
                }
                if (!object) {
                    char          child[256]{};
                    fs::file_type child_type = fs::directory_unknown;
                    if (fs::read_directory(resolved, 0, child, sizeof child,
                                           &child_type) <= 0) {
                        return cq_post_guarded(ring, s.user_data, -ENOENT);
                    }
                    __builtin_memset(reinterpret_cast<void*>(out.value.get()), 0,
                                     sizeof(astia_stat));
                    auto* directory  = reinterpret_cast<astia_stat*>(out.value.get());
                    directory->dev   = 1;
                    directory->ino   = 1;
                    directory->nlink = 1;
                    directory->mode  = mapper<fs::file_mode>::to(fs::node_mode(resolved));
                    return cq_post_guarded(ring, s.user_data, 0);
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
            auto* stat_out = reinterpret_cast<astia_stat*>(out.value.get());
            __builtin_memset(stat_out, 0, sizeof *stat_out);
            stat_out->dev        = 1;
            stat_out->ino        = object->node()->inode();
            stat_out->nlink      = object->node()->nlink();
            stat_out->size       = static_cast<u64>(size);
            stat_out->blocks     = (static_cast<u64>(size) + 511u) / 512u;
            stat_out->mode       = mapper<fs::file_mode>::to(object->mode());
            stat_out->uid        = static_cast<u32>(object->node()->uid());
            stat_out->gid        = static_cast<u32>(object->node()->gid());
            stat_out->mtime_sec  = object->mtime_sec();
            stat_out->mtime_nsec = object->mtime_nsec();
            return cq_post_guarded(ring, s.user_data, 0);
        }

        bool handle_chmod(astia_rings* ring, const astia_sqe& s) {
            auto* current = proc::current_process();
            if (current == nullptr)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            auto requested = mapper<fs::file_permissions>::from(
                s.address == 0 ? s.aux & fs::file_permissions_mask : s.aux);
            if (!requested.has_value)
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            if (s.address == 0) {
                auto mapped_fd = mapper<fd_t>::from(s.fd);
                if (!mapped_fd.has_value)
                    return cq_post_guarded(ring, s.user_data, -EBADF);
                auto object = current->descriptor(mapped_fd.value);
                if (!object)
                    return cq_post_guarded(ring, s.user_data, -EBADF);
                if (!object->node())
                    return cq_post_guarded(ring, s.user_data, -ENOSYS);
                if (current->euid() != 0 &&
                    current->euid() != static_cast<u32>(object->node()->uid()))
                    return cq_post_guarded(ring, s.user_data, -EPERM);
                return cq_post_guarded(
                    ring, s.user_data,
                    static_cast<i32>(object->chmod(requested.value)));
            }
            char path[256];
            if (!sys::copy_in_string(span<char>(path, sizeof path),
                                     sys::user_ptr<char>(s.address)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            char  resolved[256];
            if (current == nullptr ||
                current->compose_path(path, resolved, sizeof resolved) < 0)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            if (current->euid() != 0 &&
                current->euid() != static_cast<u32>(fs::node_uid(resolved)))
                return cq_post_guarded(ring, s.user_data, -EPERM);
            return cq_post_guarded(
                ring, s.user_data,
                static_cast<i32>(fs::chmod_path(resolved, requested.value)));
        }

        bool handle_chown(astia_rings* ring, const astia_sqe& s) {
            auto* current = proc::current_process();
            auto  owner   = mapper<uid_t>::from(static_cast<i64>(s.aux));
            auto  group   = mapper<gid_t>::from(static_cast<i64>(s.offset));
            if (!owner.has_value || !group.has_value)
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            if (current == nullptr)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            if (s.fd >= 0) {
                auto mapped_fd = mapper<fd_t>::from(s.fd);
                if (!mapped_fd.has_value)
                    return cq_post_guarded(ring, s.user_data, -EBADF);
                auto object = current->descriptor(mapped_fd.value);
                if (!object)
                    return cq_post_guarded(ring, s.user_data, -EBADF);
                if (current->euid() != 0 &&
                    (current->euid() != static_cast<u32>(object->node()->uid()) ||
                     owner.value != object->node()->uid() ||
                     !current->in_group(group.value)))
                    return cq_post_guarded(ring, s.user_data, -EPERM);
                return cq_post_guarded(
                    ring, s.user_data,
                    static_cast<i32>(object->node()->chown(owner.value, group.value)));
            }
            char path[256];
            if (!sys::copy_in_string(span<char>(path, sizeof path),
                                     sys::user_ptr<char>(s.address)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            char       resolved[256];
            const bool follow_final = (s.op_flags & STATX_NOFOLLOW) == 0;
            long resolve_rc = current->compose_path(path, resolved, sizeof resolved);
            if (resolve_rc >= 0)
                resolve_rc =
                    fs::resolve_path(resolved, resolved, sizeof resolved, follow_final);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
            const uid_t existing_uid = fs::node_uid(resolved);
            if (current->euid() != 0 &&
                (current->euid() != static_cast<u32>(existing_uid) ||
                 owner.value != existing_uid || !current->in_group(group.value)))
                return cq_post_guarded(ring, s.user_data, -EPERM);
            return cq_post_guarded(
                ring, s.user_data,
                static_cast<i32>(
                    fs::chown_path(resolved, owner.value, group.value, follow_final)));
        }

        bool handle_utimensat(astia_rings* ring, const astia_sqe& s) {
            char path[256];
            if (!sys::copy_in_string(span<char>(path, sizeof path),
                                     sys::user_ptr<char>(s.address)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            auto* current = proc::current_process();
            char  resolved[256];
            if (current == nullptr)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            long resolve_rc = current->compose_path(path, resolved, sizeof resolved);
            if (resolve_rc >= 0)
                resolve_rc = fs::resolve_path(resolved, resolved, sizeof resolved, true);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
            u64 sec  = 0;
            u64 nsec = 0;
            if (!sys::realtime_now(sec, nsec))
                return cq_post_guarded(ring, s.user_data, -EIO);
            return cq_post_guarded(ring, s.user_data,
                                   static_cast<i32>(fs::touch_path(resolved, sec, nsec)));
        }

        bool handle_symlinkat(astia_rings* ring, const astia_sqe& s) {
            char target[256], path[256];
            if (!sys::copy_in_string(span<char>(target, sizeof target),
                                     sys::user_ptr<char>(s.address)) ||
                !sys::copy_in_string(span<char>(path, sizeof path),
                                     sys::user_ptr<char>(s.offset)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            auto* current = proc::current_process();
            char  resolved[256];
            if (!current)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            long resolve_rc = resolve_at(current, s.fd, path, resolved, sizeof resolved);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
            resolve_rc = fs::resolve_path(resolved, resolved, sizeof resolved, false);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
            long link_rc = fs::make_symlink(target, resolved);
            if (link_rc >= 0)
                (void) fs::chown_path(resolved, uid_t{current->uid()},
                                      gid_t{current->gid()}, false);
            return cq_post_guarded(ring, s.user_data, (i32) link_rc);
        }

        bool handle_readlinkat(astia_rings* ring, const astia_sqe& s) {
            char path[256];
            if (!sys::copy_in_string(span<char>(path, sizeof path),
                                     sys::user_ptr<char>(s.address)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            auto* current = proc::current_process();
            char  resolved[256];
            if (!current)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            long resolve_rc = resolve_at(current, s.fd, path, resolved, sizeof resolved);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
            resolve_rc = fs::resolve_path(resolved, resolved, sizeof resolved, false);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
            auto out = span<u8>::bytes(s.offset, s.length);
            if (!sys::user_span_ok(out))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            // read_symlink is a kernel/VFS API; never give it a raw
            // caller address.  The fixed path limit is also the VFS's
            // maximum link length, so an oversized user capacity is
            // harmless and cannot become a kernel stack allocation.
            char       target[256]{};
            const u32  capacity = s.length < sizeof target ? s.length : sizeof target;
            const long n        = fs::read_symlink(resolved, target, capacity);
            if (n < 0)
                return cq_post_guarded(ring, s.user_data, (i32) n);
            if (n != 0 &&
                !sys::copy_out(span<u8>::bytes(s.offset, static_cast<usize>(n)), target))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            return cq_post_guarded(ring, s.user_data, (i32) n);
        }

        bool handle_readdir(astia_rings* ring, const astia_sqe& s) {
            char path[256];
            if (!sys::copy_in_string(span<char>(path, sizeof path),
                                     sys::user_ptr<char>(s.offset)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            auto* current = proc::current_process();
            char  resolved[256];
            if (current == nullptr)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            long resolve_rc = current->compose_path(path, resolved, sizeof resolved);
            if (resolve_rc >= 0)
                resolve_rc = fs::resolve_path(resolved, resolved, sizeof resolved, true);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
            const auto out = sys::user_ptr<astia_dirent>(s.address).validate();
            if (!out)
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            astia_dirent  result{};
            fs::file_type type = fs::directory_unknown;
            long rc     = fs::read_directory(resolved, s.fd < 0 ? 0 : s.fd, result.name,
                                             sizeof result.name, &type);
            result.type = static_cast<u32>(type);
            if (rc > 0) {
                u32 length = 0;
                while (length < sizeof result.name && result.name[length] != '\0')
                    ++length;
                result.namelen = length;
            }
            if (!sys::copy_out(out.value.bytes(), &result))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            return cq_post_guarded(ring, s.user_data, static_cast<i32>(rc));
        }

        bool handle_close(astia_rings* ring, const astia_sqe& s) {
            proc::process* current = proc::current_process();
            auto           mapped  = mapper<fd_t>::from(s.fd);
            if (current == nullptr || !mapped.has_value ||
                !current->descriptor(mapped.value)) {
                return cq_post_guarded(ring, s.user_data, -EBADF);
            }
            current->clear_descriptor(mapped.value);
            return cq_post_guarded(ring, s.user_data, 0);
        }

        bool handle_ftruncate(astia_rings* ring, const astia_sqe& s) {
            auto object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            if ((object->flags() & fs::open_file_access_mask) == 0)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            return cq_post_guarded(ring, s.user_data,
                                   static_cast<i32>(object->truncate(s.offset)));
        }

        bool handle_fsync(astia_rings* ring, const astia_sqe& s) {
            auto object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            if (!object->mode().is_regular())
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            return cq_post_guarded(ring, s.user_data, static_cast<i32>(object->sync()));
        }

        bool handle_dup(astia_rings* ring, const astia_sqe& s) {
            proc::process*           current = proc::current_process();
            mm::shptr<fs::open_file> source  = file_for(s.fd);
            if (current == nullptr || !source) {
                return cq_post_guarded(ring, s.user_data, -EBADF);
            }
            const auto fd = current->duplicate_descriptor(mapper<fd_t>::from(s.fd).value);
            return cq_post_guarded(ring, s.user_data,
                                   fd ? static_cast<i64>(fd.value.value) : -EMFILE);
        }

        bool handle_dup2(astia_rings* ring, const astia_sqe& s) {
            proc::process* current = proc::current_process();
            auto           old_fd  = mapper<fd_t>::from(s.fd);
            auto           new_fd  = mapper<fd_t>::from(static_cast<i64>(s.offset));
            if (current == nullptr || !old_fd.has_value || !new_fd.has_value ||
                !current->descriptor(old_fd.value)) {
                return cq_post_guarded(ring, s.user_data, -EBADF);
            }
            const i32 target = new_fd.value.value;
            if (target != s.fd) {
                const auto result =
                    current->duplicate_descriptor_to(old_fd.value, new_fd.value);
                if (!result)
                    return cq_post_guarded(ring, s.user_data, -EMFILE);
            }
            return cq_post_guarded(ring, s.user_data, target);
        }

        bool handle_fcntl(astia_rings* ring, const astia_sqe& s) {
            proc::process* current = proc::current_process();
            auto           mapped  = mapper<fd_t>::from(s.fd);
            if (current == nullptr || !mapped.has_value ||
                !current->descriptor(mapped.value))
                return cq_post_guarded(ring, s.user_data, -EBADF);
            const u32 argument = s.aux;
            switch (s.op_flags) {
            case ASTIA_F_DUPFD:
            case ASTIA_F_DUPFD_CLOEXEC: {
                if (static_cast<i32>(argument) < 0)
                    return cq_post_guarded(ring, s.user_data, -EINVAL);
                const u32 flags =
                    s.op_flags == ASTIA_F_DUPFD_CLOEXEC ? ASTIA_FD_CLOEXEC : 0;
                const auto result = current->duplicate_descriptor(
                    mapped.value, fd_t{static_cast<i32>(argument)}, flags);
                return cq_post_guarded(ring, s.user_data,
                                       result ? result.value.value : -EMFILE);
            }
            case ASTIA_F_GETFD:
                return cq_post_guarded(
                    ring, s.user_data,
                    static_cast<i32>(current->descriptor_flags(mapped.value)));
            case ASTIA_F_SETFD:
                if ((argument & ~ASTIA_FD_CLOEXEC) != 0)
                    return cq_post_guarded(ring, s.user_data, -EINVAL);
                if (!current->set_descriptor_flags(mapped.value, argument))
                    return cq_post_guarded(ring, s.user_data, -EBADF);
                return cq_post_guarded(ring, s.user_data, 0);
            case ASTIA_F_GETFL: {
                const u32 flags = current->descriptor(mapped.value)->flags();
                u32 result;
                if ((flags & fs::pipe_write_endpoint_flag) != 0)
                    result = ASTIA_AIO_O_WRONLY;
                else if ((flags & fs::pipe_read_endpoint_flag) != 0)
                    result = ASTIA_AIO_O_RDONLY;
                else
                    result = (flags & ASTIA_AIO_O_ACCMODE) == ASTIA_AIO_O_RDWR
                                 ? ASTIA_AIO_O_RDWR
                                 : ((flags & ASTIA_AIO_O_WRONLY) ? ASTIA_AIO_O_WRONLY
                                                                 : ASTIA_AIO_O_RDONLY);
                if (flags & static_cast<u32>(fs::open_file_status_flags::append))
                    result |= ASTIA_O_APPEND;
                if (flags & static_cast<u32>(fs::open_file_status_flags::nonblock))
                    result |= ASTIA_O_NONBLOCK;
                return cq_post_guarded(ring, s.user_data, static_cast<i32>(result));
            }
            case ASTIA_F_SETFL: {
                auto object = current->descriptor(mapped.value);
                object->set_status_flags(
                    (argument & ASTIA_O_APPEND
                         ? static_cast<u32>(fs::open_file_status_flags::append)
                         : 0u) |
                    (argument & ASTIA_O_NONBLOCK
                         ? static_cast<u32>(fs::open_file_status_flags::nonblock)
                         : 0u));
                return cq_post_guarded(ring, s.user_data, 0);
            }
            default:
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            }
        }

        bool handle_pipe(astia_rings* ring, const astia_sqe& s) {
            const auto out = sys::user_ptr<i32>(s.address).validate(2);
            if (!out)
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            proc::process* current = proc::current_process();
            if (current == nullptr)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            mm::shptr<fs::open_file> read_end;
            mm::shptr<fs::open_file> write_end;
            long                     rc = fs::create_pipe(read_end, write_end);
            if (rc < 0)
                return cq_post_guarded(ring, s.user_data, static_cast<i32>(rc));
            i32 read_fd  = -1;
            i32 write_fd = -1;
            for (i32 fd = 3; fd < static_cast<i32>(proc::max_process_fds); ++fd) {
                if (read_fd < 0 && !current->descriptor(fd_t{fd})) {
                    if (current->set_descriptor(fd_t{fd}, read_end))
                        read_fd = fd;
                }
                else if (write_fd < 0 && !current->descriptor(fd_t{fd})) {
                    if (current->set_descriptor(fd_t{fd}, write_end)) {
                        write_fd = fd;
                        break;
                    }
                }
            }
            if (read_fd < 0 || write_fd < 0) {
                if (read_fd >= 0)
                    current->clear_descriptor(fd_t{read_fd});
                return cq_post_guarded(ring, s.user_data, -EMFILE);
            }
            i32 result[2] = {read_fd, write_fd};
            if (!sys::copy_out(out.value.bytes(2), result)) {
                current->clear_descriptor(fd_t{read_fd});
                current->clear_descriptor(fd_t{write_fd});
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            }
            return cq_post_guarded(ring, s.user_data, 0);
        }

        // Keep the userspace SQE as an integer-only ABI, but do not let the
        // socket policy below operate on unrelated raw fields.  These types
        // are deliberately private: the filesystem socket API still takes
        // the public u32 socket type.
        enum class socket_domain : u32 {
            unix_domain = AF_UNIX,
        };
        enum class socket_type : u32 {
            stream   = SOCK_STREAM,
            datagram = SOCK_DGRAM,
        };
        struct socket_request {
            socket_domain domain;
            socket_type   type;
            u32           aux;

            static constexpr socket_request from_sqe(const astia_sqe& s) {
                return {static_cast<socket_domain>(static_cast<u32>(s.fd)),
                        static_cast<socket_type>(s.op_flags), s.aux};
            }

            constexpr bool supported() const {
                return domain == socket_domain::unix_domain &&
                       (type == socket_type::stream || type == socket_type::datagram) &&
                       aux == 0;
            }
        };

        static_assert(static_cast<u32>(socket_domain::unix_domain) == AF_UNIX);
        static_assert(static_cast<u32>(socket_type::stream) == SOCK_STREAM);
        static_assert(static_cast<u32>(socket_type::datagram) == SOCK_DGRAM);

        bool handle_socket(astia_rings* ring, const astia_sqe& s) {
            const socket_request request = socket_request::from_sqe(s);
            if (!request.supported())
                return cq_post_guarded(ring, s.user_data, -EAFNOSUPPORT);
            proc::process*           current = proc::current_process();
            mm::shptr<fs::open_file> object;
            long rc = fs::create_unix_socket(object, static_cast<u32>(request.type));
            if (rc < 0 || !current)
                return cq_post_guarded(ring, s.user_data, (i32) (rc < 0 ? rc : -ESRCH));
            for (i32 fd = 3; fd < (i32) proc::max_process_fds; ++fd)
                if (!current->descriptor(fd_t{fd}) &&
                    current->set_descriptor(fd_t{fd}, object))
                    return cq_post_guarded(ring, s.user_data, fd);
            return cq_post_guarded(ring, s.user_data, -EMFILE);
        }

        bool handle_eventfd_create(astia_rings* ring, const astia_sqe& s) {
            if ((s.op_flags & ~(ASTIA_EFD_NONBLOCK | ASTIA_EFD_CLOEXEC)) != 0 ||
                s.offset == UINT64_MAX)
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            proc::process* current = proc::current_process();
            if (!current)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            mm::shptr<eventfd_file> event(
                new eventfd_file(s.offset, (s.op_flags & ASTIA_EFD_NONBLOCK) != 0));
            if (!event)
                return cq_post_guarded(ring, s.user_data, -ENOMEM);
            const u32 status_flags =
                s.op_flags & ASTIA_EFD_NONBLOCK
                    ? static_cast<u32>(fs::open_file_status_flags::nonblock)
                    : 0u;
            const u32 descriptor_flags =
                s.op_flags & ASTIA_EFD_CLOEXEC ? static_cast<u32>(ASTIA_FD_CLOEXEC) : 0u;
            auto object = mm::share<fs::open_file>(
                event, static_cast<fs::open_file_status_flags>(status_flags));
            for (i32 fd = 3; fd < (i32) proc::max_process_fds; ++fd)
                if (!current->descriptor(fd_t{fd}) &&
                    current->set_descriptor(fd_t{fd}, object, descriptor_flags))
                    return cq_post_guarded(ring, s.user_data, fd);
            return cq_post_guarded(ring, s.user_data, -EMFILE);
        }

        bool handle_socketpair(astia_rings* ring, const astia_sqe& s) {
            if (s.fd != AF_UNIX)
                return cq_post_guarded(ring, s.user_data, -EAFNOSUPPORT);
            if (s.aux != 0)
                return cq_post_guarded(ring, s.user_data, -EPROTONOSUPPORT);
            if (s.op_flags != SOCK_STREAM && s.op_flags != SOCK_DGRAM)
                return cq_post_guarded(ring, s.user_data, -EPROTOTYPE);
            if (s.length != 2 * sizeof(i32))
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            if (!sys::user_span_writable_ok_for(
                    proc::current_process(), span<u8>::bytes(s.address, s.length)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            proc::process* current = proc::current_process();
            if (!current)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            mm::shptr<fs::open_file> a, b;
            long                     rc = fs::unix_socketpair(a, b, s.op_flags);
            if (rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) rc);
            i32 fds[2] = {-1, -1};
            for (i32 fd = 3; fd < (i32) proc::max_process_fds && fds[1] < 0; ++fd) {
                if (fds[0] < 0 && !current->descriptor(fd_t{fd}) &&
                    current->set_descriptor(fd_t{fd}, a))
                    fds[0] = fd;
                else if (fds[0] >= 0 && !current->descriptor(fd_t{fd}) &&
                         current->set_descriptor(fd_t{fd}, b))
                    fds[1] = fd;
            }
            if (fds[1] < 0) {
                if (fds[0] >= 0)
                    current->clear_descriptor(fd_t{fds[0]});
                return cq_post_guarded(ring, s.user_data, -EMFILE);
            }
            if (!sys::copy_out(span<u8>::bytes(s.address, s.length), fds)) {
                current->clear_descriptor(fd_t{fds[0]});
                current->clear_descriptor(fd_t{fds[1]});
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            }
            return cq_post_guarded(ring, s.user_data, 0);
        }

        bool handle_sendto_recvfrom(ring_slot& slot, astia_rings* ring,
                                    const astia_sqe& s) {
            mm::shptr<fs::open_file> object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            const auto address = sys::user_ptr<astia_sockaddr>(s.offset).validate();
            if (!sys::user_span_ok(span<u8>::bytes(s.address, s.length)) || !address)
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            if (s.length > max_operation_bytes)
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            astia_sockaddr a{};
            if (s.op == OP_SENDTO) {
                if (!address || !sys::copy_in(&a, address.value.bytes()))
                    return cq_post_guarded(ring, s.user_data, -EFAULT);
                char socket_path[fs::path_max]{}, canonical[fs::path_max]{};
                if (a.family != AF_UNIX || a.len < 3 || a.len > sizeof(a.data) + 2)
                    return cq_post_guarded(ring, s.user_data, -EINVAL);
                const u32 path_length = a.len - 2;
                if (path_length >= sizeof socket_path || a.data[0] != '/')
                    return cq_post_guarded(ring, s.user_data, -EINVAL);
                for (u32 i = 0; i < path_length; ++i)
                    socket_path[i] = static_cast<char>(a.data[i]);
                socket_path[path_length] = '\0';
                long path_rc =
                    fs::resolve_path(socket_path, canonical, sizeof canonical, false);
                if (path_rc < 0)
                    return cq_post_guarded(ring, s.user_data, static_cast<i32>(path_rc));
                if (!socket_node_writable(proc::current_process(), canonical))
                    return cq_post_guarded(ring, s.user_data, -EACCES);
                const auto* socket = object->node()->unix_socket_capability();
                long rc = socket == nullptr ? -EOPNOTSUPP : socket->sendto(*object, &a);
                if (rc < 0)
                    return cq_post_guarded(ring, s.user_data, static_cast<i32>(rc));
            }
            else {
                if (s.length == 0)
                    return cq_post_guarded(ring, s.user_data, 0);
                const auto* socket = object->node()->unix_socket_capability();
                long rc = socket == nullptr ? -EOPNOTSUPP : socket->recvfrom(*object);
                if (rc < 0)
                    return cq_post_guarded(ring, s.user_data, static_cast<i32>(rc));
            }
            span<u8> range = span<u8>::bytes(s.address, s.length);
            return submit_request(slot, ring, s, object, &range, 1,
                                  s.op == OP_RECVFROM ? io_direction::input
                                                      : io_direction::output);
        }

        bool handle_bind_connect(astia_rings* ring, const astia_sqe& s) {
            auto object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            const auto address = sys::user_ptr<astia_sockaddr>(s.address).validate();
            if (!address)
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            astia_sockaddr a{};
            if (!sys::copy_in(&a, address.value.bytes()))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            char socket_path[fs::path_max]{}, canonical[fs::path_max]{};
            if (a.family != AF_UNIX || a.len < 3 || a.len > sizeof(a.data) + 2)
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            const u32 path_length = a.len - 2;
            if (path_length >= sizeof socket_path || a.data[0] != '/')
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            for (u32 i = 0; i < path_length; ++i)
                socket_path[i] = static_cast<char>(a.data[i]);
            socket_path[path_length] = '\0';
            long path_rc =
                fs::resolve_path(socket_path, canonical, sizeof canonical, false);
            if (path_rc < 0)
                return cq_post_guarded(ring, s.user_data, static_cast<i32>(path_rc));
            if (s.op == OP_BIND) {
                if (!parent_writable(proc::current_process(), canonical))
                    return cq_post_guarded(ring, s.user_data, -EACCES);
            }
            else if (!socket_node_writable(proc::current_process(), canonical)) {
                return cq_post_guarded(ring, s.user_data, -EACCES);
            }
            const auto* socket = object->node()->unix_socket_capability();
            long rc = socket == nullptr ? -EOPNOTSUPP
                                        : (s.op == OP_BIND ? socket->bind(*object, a)
                                                           : socket->connect(*object, a));
            return cq_post_guarded(ring, s.user_data, (i32) rc);
        }

        bool handle_listen(astia_rings* ring, const astia_sqe& s) {
            auto object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            const auto* socket = object->node()->unix_socket_capability();
            return cq_post_guarded(
                ring, s.user_data,
                static_cast<i32>(socket == nullptr ? -EOPNOTSUPP
                                                   : socket->listen(*object, s.aux)));
        }

        bool handle_accept(ring_slot& slot, astia_rings* ring, const astia_sqe& s) {
            auto object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            if (object->node()->unix_socket_capability() == nullptr)
                return cq_post_guarded(ring, s.user_data, -EOPNOTSUPP);
            return submit_request(slot, ring, s, object, nullptr, 0, io_direction::input,
                                  true, true);
        }

        bool handle_getsockopt(astia_rings* ring, const astia_sqe& s) {
            auto object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            auto length = sys::user_ptr<u32>(s.offset).validate();
            if (!length || !sys::user_span_writable_ok_for(
                              proc::current_process(), length.value.bytes()))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            u32 capacity = 0;
            if (!sys::copy_in(&capacity, length.value.bytes()))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            if (capacity > max_operation_bytes)
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            const auto* socket = object->node()->unix_socket_capability();
            if (socket == nullptr)
                return cq_post_guarded(ring, s.user_data, -ENOTSOCK);
            u32 value = 0;
            long rc = socket->getsockopt(*object, s.op_flags, s.aux, &value);
            if (rc < 0)
                return cq_post_guarded(ring, s.user_data, static_cast<i32>(rc));
            const u32 result_length = capacity < sizeof value ? capacity : sizeof value;
            if (!sys::copy_out_for(proc::current_process(),
                                   span<u8>::bytes(s.address, result_length), &value) ||
                !sys::copy_out_for(proc::current_process(), length.value.bytes(),
                                   &result_length))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            return cq_post_guarded(ring, s.user_data, 0);
        }

        bool handle_shutdown(astia_rings* ring, const astia_sqe& s) {
            auto object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            const auto* socket = object->node()->unix_socket_capability();
            return cq_post_guarded(
                ring, s.user_data,
                static_cast<i32>(socket == nullptr
                                     ? -EOPNOTSUPP
                                     : socket->shutdown(*object, s.op_flags)));
        }

        bool handle_ioctl(ring_slot& slot, astia_rings* ring, const astia_sqe& s) {
            /* ioctl is a control-plane fence.  Do not consume this SQE
               while an older read/write is parked: otherwise a batch
               containing READ, IOCTL, READ could observe tty state out
               of order.  Returning false leaves SQ head at IOCTL; the
               wake-up path retries it after the parked operation posts. */
            if (has_parked(slot))
                return false;
            // ioctl implementations expect a stable kernel object and
            // may both read and write it.  Decode the ABI's typed
            // requests here instead of passing a caller pointer into a
            // device (which was both a TOCTOU and kernel-pointer bug).
            usize expected = 0;
            bool  input = false, output = false;
            if (s.offset == TCGETS || s.offset == TCSETS || s.offset == TCSETSW ||
                s.offset == TCSETSF) {
                expected = sizeof(struct termios);
                input    = s.offset != TCGETS;
                output   = s.offset == TCGETS;
            }
            else if (s.offset == TIOCGWINSZ || s.offset == TIOCSWINSZ) {
                expected = sizeof(struct winsize);
                input    = s.offset == TIOCSWINSZ;
                output   = s.offset == TIOCGWINSZ;
            }
            else if (s.offset == TIOCGPGRP || s.offset == TIOCSPGRP) {
                expected = sizeof(i32);
                input    = s.offset == TIOCSPGRP;
                output   = s.offset == TIOCGPGRP;
            }
            else if (s.offset == TIOCSCTTY) {
                expected = 0;
            }
            else {
                return cq_post_guarded(ring, s.user_data, -ENOTTY);
            }
            const auto argument = sys::user_ptr<u8>(s.address).validate(expected);
            if (s.length != expected || !argument)
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            mm::shptr<fs::open_file> object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            alignas(8) u8 ioctl_storage[sizeof(struct termios)]{};
            if (input && !sys::copy_in(ioctl_storage, argument.value.bytes(expected)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            void* kernel_arg =
                expected == 0 ? nullptr : static_cast<void*>(ioctl_storage);
            long rc = object->ioctl(s.offset, kernel_arg, expected);
            if (rc >= 0 && s.offset == TIOCSCTTY)
                rc = proc::acquire_controlling_terminal(object);
            if (rc >= 0 && output &&
                !sys::copy_out(argument.value.bytes(expected), ioctl_storage))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            return cq_post_guarded(ring, s.user_data, static_cast<i32>(rc));
        }

        bool handle_poll_add(ring_slot& slot, astia_rings* ring, const astia_sqe& s) {
            auto object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, POLLNVAL);
            const u64 generation = ready_generation();
            const u32 ready      = object->poll(s.aux);
            if (ready != 0)
                return cq_post_guarded(ring, s.user_data, static_cast<i32>(ready));
            parked_op* p          = nullptr;
            u32        park_index = 0;
            for (u32 i = 0; i < parked_operation_slots; ++i)
                if (!slot.park[i].used) {
                    p          = &slot.park[i];
                    park_index = i;
                    break;
                }
            if (p == nullptr)
                return cq_post_guarded(ring, s.user_data, -EBUSY);
            p->used        = true;
            p->done        = false;
            p->resuming    = false;
            p->control     = true;
            p->signal_interruptible = true;
            p->readiness   = true;
            p->poll_events = s.aux;
            p->fd          = s.fd;
            p->object      = object;
            p->request     = io_request(nullptr, 0, 0, s.user_data, io_direction::input);
            publish_waiter_hint(slot, park_index, object->node().get());
            if (ready_generation() != generation)
                resume_parked(ring, *p);
            return true;
        }

        bool handle_sleep(ring_slot& slot, astia_rings* ring, const astia_sqe& s) {
            if (s.fd != -1 || (s.address == 0 ? s.length != 0
                                              : s.length != sizeof(u64)) ||
                (s.op_flags & ~SLEEP_ABSOLUTE) != 0 ||
                s.aux != 0)
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            if (s.address != 0 &&
                !sys::user_span_ok(span<u8>::bytes(s.address, sizeof(u64))))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            if (arch::timer_hz() == 0)
                return cq_post_guarded(ring, s.user_data, -EIO);
            const bool absolute = (s.op_flags & SLEEP_ABSOLUTE) != 0;
            const u64  now      = poll_now_ns();
            if ((!absolute && s.offset == 0) || (absolute && s.offset <= now))
                return cq_post_guarded(ring, s.user_data, 0);

            parked_op* p = nullptr;
            for (u32 i = 0; i < parked_operation_slots; ++i) {
                if (!slot.park[i].used) {
                    p = &slot.park[i];
                    break;
                }
            }
            if (p == nullptr)
                return cq_post_guarded(ring, s.user_data, -EBUSY);

            p->used           = true;
            p->done           = false;
            p->resuming       = false;
            p->control        = true;
            p->signal_interruptible = true;
            p->sleeping       = true;
            p->sleep_absolute = absolute;
            p->sleep_deadline = absolute ? s.offset
                                         : (UINT64_MAX - now < s.offset ? UINT64_MAX
                                                                        : now + s.offset);
            p->sleep_remaining_user = absolute ? 0 : s.address;
            p->fd             = -1;
            p->request        = io_request(nullptr, 0, 0, s.user_data, io_direction::input);
            return true;
        }

        bool handle_cancel(ring_slot& slot, astia_rings* ring, const astia_sqe& s) {
            const u64 target = s.offset;
            for (u32 i = 0; i < parked_operation_slots; ++i) {
                parked_op& p = slot.park[i];
                if (p.used && !p.done && p.request.user_data() == target) {
                    // Cancellation needs two CQ slots.  Do not consume the
                    // SQE if either completion could be lost.
                    astia_cq_ring* cq   = astia_rings_cq(ring);
                    const u32      head = __atomic_load_n(&cq->head, __ATOMIC_ACQUIRE);
                    const u32      tail = __atomic_load_n(&cq->tail, __ATOMIC_RELAXED);
                    if (tail - head > cq->entries - 2)
                        return false;
                    if (!cq_post(ring, target, -ECANCELED))
                        return false;
                    p.request.cancel_device();
                    p.done = true;
                    return cq_post(ring, s.user_data, 0);
                }
            }
            return cq_post_guarded(ring, s.user_data, -ENOENT);
        }

        bool dispatch_one(ring_slot& slot, astia_rings* ring, const astia_sqe& s) {
            switch (s.op) {
            case OP_NOP:
                return cq_post_guarded(ring, s.user_data, 0);

            case OP_READ:
            case OP_WRITE:
            case OP_SEND:
            case OP_RECV:
            case OP_READV:
            case OP_WRITEV: {
                dispatch_context context{slot, ring, s, proc::current_process()};
                return dispatch_io(context);
            }

            case OP_SENDTO:
            case OP_RECVFROM: {
                return handle_sendto_recvfrom(slot, ring, s);
            }

            case OP_OPENAT:
                return handle_openat(ring, s);

            case OP_CLOSE:
                return handle_close(ring, s);

            case OP_FTRUNCATE:
                return handle_ftruncate(ring, s);

            case OP_FSYNC:
                return handle_fsync(ring, s);

            case OP_MKDIRAT:
                return handle_mkdirat(slot, ring, s);

            case OP_UNLINKAT:
                return handle_unlinkat(slot, ring, s);

            case OP_RENAMEAT:
                return handle_renameat(slot, ring, s);

            case OP_LINKAT:
                return handle_linkat(slot, ring, s);

            case OP_DUP:
                return handle_dup(ring, s);

            case OP_DUP2:
                return handle_dup2(ring, s);

            case OP_FCNTL:
                return handle_fcntl(ring, s);

            case OP_STATX:
                return handle_statx(ring, s);

            case OP_CHMOD:
                return handle_chmod(ring, s);

            case OP_CHOWN:
                return handle_chown(ring, s);

            case OP_UTIMENSAT:
                return handle_utimensat(ring, s);

            case OP_SYMLINKAT:
                return handle_symlinkat(ring, s);
            case OP_READLINKAT:
                return handle_readlinkat(ring, s);

            case OP_READDIR:
                return handle_readdir(ring, s);

            case OP_IOCTL:
                return handle_ioctl(slot, ring, s);

            case OP_PIPE:
                return handle_pipe(ring, s);

            case OP_SOCKET:
                return handle_socket(ring, s);
            case OP_BIND:
            case OP_CONNECT: {
                return handle_bind_connect(ring, s);
            }
            case OP_LISTEN:
                return handle_listen(ring, s);
            case OP_ACCEPT:
                return handle_accept(slot, ring, s);

            case OP_POLL_ADD:
                return handle_poll_add(slot, ring, s);

            case OP_SLEEP:
                return handle_sleep(slot, ring, s);

            case OP_EVENTFD_CREATE:
                return handle_eventfd_create(ring, s);

            case OP_CANCEL:
                return handle_cancel(slot, ring, s);
            case OP_SOCKETPAIR:
                return handle_socketpair(ring, s);
            case OP_SHUTDOWN:
                return handle_shutdown(ring, s);
            case OP_GETSOCKOPT:
                return handle_getsockopt(ring, s);

            default:
                return cq_post_guarded(ring, s.user_data, -ENOSYS);
            }
        }

    } // namespace

    long io_setup(u32 entries, u32 flags, astia_rings** out) {
        return aio_ring::setup(entries, flags, out);
    }

    long io_destroy(astia_rings* ring) {
        return aio_ring::destroy(ring);
    }

    long io_submit(astia_rings* ring, u32 to_submit) {
        ring_guard pool_guard(g_ring_pool_lock);
        ring_slot* slot = slot_of(ring);
        if (slot == nullptr || !ring_layout_ok(*slot, ring)) {
            return -EINVAL;
        }
        ring_guard ring_lock_guard(slot->lock);
        // The ring lock pins the slot while the submitter runs.  The pool is
        // needed only for lookup/lifetime, not for dispatch or CQ work.
        pool_guard.release_lock();

        retry_pending(*slot, rings_of(*slot));

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

        // Inline completions and pin failures use park storage so their
        // resource release is deferred just like an IRQ completion.  Detach
        // them while protected, then release after unlocking below.
        // This single cleanup generation also reclaims completions left by a
        // previous enter.  Keeping an earlier generation alive on this small
        // kernel stack would both overwrite resources when slots are reused
        // and exceed the 16 KiB stack budget.
        parked_cleanup& cleanup = slot->cleanup;
        reap_completed(*slot, cleanup);
        cq_retrigger(*slot, rings_of(*slot));

        ring_lock_guard.unlock();
        cleanup.release();
        return static_cast<long>(consumed);
    }

    void io_device_ready(i32 fd) {
        proc::process* current = proc::current_process();
        auto current_object    = current == nullptr ? mm::shptr<fs::open_file>{}
                                                    : current->descriptor(fd_t{fd});
        if (!current_object) {
            return;
        }
        const fs::file* device = current_object->node().get();
        bool            indexed[kernel_ring_slot_count][parked_operation_slots]{};
        note_device_ready(device);
        snapshot_waiters(device, indexed);
        for (u32 i = 0; i < kernel_ring_slot_count; ++i) {
            for (u32 p = 0; p < parked_operation_slots; ++p) {
                if (!indexed[i][p])
                    continue;
                ring_guard pool_guard(g_ring_pool_lock);
                ring_slot* slot_ptr = g_slots[i];
                if (slot_ptr == nullptr || !slot_ptr->used)
                    continue;
                ring_slot& slot = *slot_ptr;
                ring_guard ring_lock_guard(slot.lock);
                pool_guard.release_lock();
                parked_op& op = slot.park[p];
                if (op.used && !op.done && op.fd == fd && op.object &&
                    op.object->node().get() == device) {
                    resume_parked(rings_of(slot), op);
                }
                cq_retrigger(slot, rings_of(slot));
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
        bool indexed[kernel_ring_slot_count][parked_operation_slots]{};
        note_device_ready(dev);
        snapshot_waiters(dev, indexed);
        for (u32 i = 0; i < kernel_ring_slot_count; ++i) {
            for (u32 p = 0; p < parked_operation_slots; ++p) {
                if (!indexed[i][p])
                    continue;
                ring_guard pool_guard(g_ring_pool_lock);
                ring_slot* slot_ptr = g_slots[i];
                if (slot_ptr == nullptr || !slot_ptr->used)
                    continue;
                ring_slot& slot = *slot_ptr;
                ring_guard ring_lock_guard(slot.lock);
                pool_guard.release_lock();
                parked_op& op = slot.park[p];
                if (op.used && !op.done && op.object &&
                    op.request.user_data() != excluded_user_data &&
                    op.object->node().get() == dev) {
                    resume_parked(rings_of(slot), op);
                }
                cq_retrigger(slot, rings_of(slot));
            }
        }
    }

    void io_interrupt_process(proc::process* owner, i32 result) {
        if (owner == nullptr)
            return;
        for (u32 i = 0; i < kernel_ring_slot_count; ++i) {
            ring_guard pool_guard(g_ring_pool_lock);
            ring_slot* slot_ptr = g_slots[i];
            if (slot_ptr == nullptr || !slot_ptr->used || slot_ptr->owner.get() != owner)
                continue;
            ring_slot& slot = *slot_ptr;
            ring_guard ring_lock_guard(slot.lock);
            pool_guard.release_lock();
            astia_rings* ring = rings_of(slot);
            for (u32 p = 0; p < parked_operation_slots; ++p) {
                parked_op& op = slot.park[p];
                if (op.used && !op.done && op.signal_interruptible)
                    complete_control(owner, ring, op, result);
            }
            cq_retrigger(slot, ring);
        }
    }

    void io_poll_tick() {
        if (arch::timer_hz() == 0)
            return;
        const u64 now = poll_now_ns();
        for (u32 i = 0; i < kernel_ring_slot_count; ++i) {
            ring_guard pool_guard(g_ring_pool_lock);
            ring_slot* slot_ptr = g_slots[i];
            if (slot_ptr == nullptr || !slot_ptr->used)
                continue;
            ring_slot& slot = *slot_ptr;
            ring_guard ring_lock_guard(slot.lock);
            pool_guard.release_lock();
            astia_rings* ring = rings_of(slot);
            retry_pending(slot, ring);
            for (u32 p = 0; p < parked_operation_slots; ++p) {
                parked_op& op = slot.park[p];
                if (op.used && !op.done && op.sleeping && now >= op.sleep_deadline)
                    complete_control(nullptr, ring, op, 0);
                if (op.used && !op.done && op.timed && now >= op.timeout_deadline) {
                    op.timed = false;
                    op.request.timeout_expired(true);
                    resume_parked(ring, op);
                }
            }
            cq_retrigger(slot, ring);
        }
    }

    void io_exec_process(proc::process* owner) {
        if (owner == nullptr)
            return;
        for (u32 i = 0; i < kernel_ring_slot_count; ++i) {
            ring_guard pool_guard(g_ring_pool_lock);
            ring_slot* slot = g_slots[i];
            if (slot == nullptr || !slot->used || slot->owner.get() != owner)
                continue;
            ring_guard ring_lock_guard(slot->lock);
            pool_guard.release_lock();
            parked_cleanup& cleanup = slot->cleanup;
            reap_completed(*slot, cleanup);
            unmap_ring_user(*slot, owner);
            clear_slot(*slot, cleanup);
            ring_lock_guard.unlock();
            cleanup.release();
        }
    }

    void io_exit_process(proc::process* owner) {
        if (owner == nullptr)
            return;
        for (u32 i = 0; i < kernel_ring_slot_count; ++i) {
            ring_guard pool_guard(g_ring_pool_lock);
            ring_slot* slot = g_slots[i];
            if (slot == nullptr || !slot->used || slot->owner.get() != owner)
                continue;
            ring_guard ring_lock_guard(slot->lock);
            pool_guard.release_lock();
            parked_cleanup& cleanup = slot->cleanup;
            reap_completed(*slot, cleanup);
            // No operation may outlive its process.  In particular, a parked
            // pipe read holds an open_file reference; leaving the ring slot
            // behind until waitpid would keep that endpoint alive and prevent
            // EOF from reaching the other end.  The process is gone, so cancel
            // all parked operations while the slot is still owned by it.
            unmap_ring_user(*slot, owner);
            clear_slot(*slot, cleanup);
            g_slots[i] = nullptr;
            ring_lock_guard.unlock();
            cleanup.release();
            slot->~ring_slot();
        }
    }

} // namespace astia::sys
