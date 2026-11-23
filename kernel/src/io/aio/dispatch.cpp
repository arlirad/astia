#include "dispatch.hpp"

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

#include "../../fs/mapping.hpp"
#include "../../ipc/unix_socket.hpp"
#include "../../proc/mapping.hpp"
#include "../../proc/proc.hpp"
#include "memory.hpp"
#include "registry.hpp"

void* operator new(astia::usize size, void* where) noexcept;

namespace astia::io::aio::dispatch {

    using namespace ring;

    namespace detail {
        u64 next_request_generation = 1;

        u64 issue_request_generation() {
            const u64 result = next_request_generation;
            next_request_generation =
                next_request_generation == ~u64{0} ? 1 : next_request_generation + 1;
            return result;
        }

        void wake_request(void* context) {
            if (context != nullptr)
                io_device_ready_context(static_cast<io_device*>(context));
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

        constexpr usize max_operation_bytes = ASTIA_AIO_MAX_OPERATION_BYTES;

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
            op.operation->request().timeout_expired(false);
            op.operation->request().poll_wakeup(false);
            if (!op.operation->request().timeout_set() ||
                op.operation->request().timeout_ns() == 0) {
                op.timed = false;
            }
            else {
                const u64 now = poll_now_ns();
                op.timed      = true;
                op.timeout_deadline =
                    UINT64_MAX - now < op.operation->request().timeout_ns()
                        ? UINT64_MAX
                        : now + op.operation->request().timeout_ns();
            }
            if (!op.operation->request().poll_interval_set() ||
                op.operation->request().poll_interval_ns() == 0) {
                op.poll_timed = false;
            }
            else {
                const u64 now = poll_now_ns();
                op.poll_timed = true;
                op.poll_deadline =
                    UINT64_MAX - now < op.operation->request().poll_interval_ns()
                        ? UINT64_MAX
                        : now + op.operation->request().poll_interval_ns();
            }
        }

        void complete_control(proc::process* owner, astia_rings* ring, parked_op& op,
                              i32 result) {
            if (op.type != parked_op::kind::operation || op.operation == nullptr)
                return;
            if (op.done || op.pending_cqe)
                return;
            if (result == -EINTR && op.sleeping && !op.sleep_absolute &&
                op.sleep_remaining_user != 0) {
                const u64 now = poll_now_ns();
                const u64 remaining =
                    now >= op.sleep_deadline ? 0 : op.sleep_deadline - now;
                if (!memory::copy_out_for(
                        owner, span<u8>::bytes(op.sleep_remaining_user, sizeof remaining),
                        &remaining))
                    result = -EFAULT;
            }
            if (op.operation->request().completion() != nullptr) {
                if (result < 0)
                    op.operation->request().completion()->fail_deferred(
                        error_from_errno(-result));
                else
                    op.operation->request().completion()->complete_deferred(
                        static_cast<i64>(result));
                op.done = true;
            }
            else if (!cq_post(ring, op.operation->request().user_data(),
                              static_cast<i32>(result))) {
                op.pending_cqe       = true;
                op.pending_user_data = op.operation->request().user_data();
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
        /// (see ::astia::io::request::complete); `result` is the operation's answer
        /// once it completes — bytes moved, or -errno — and is untouched
        /// while it is parked.
        ///
        /// `op.operation->request()` must describe the stretch about to be submitted. The
        /// resume path hands it the request the device parked on, which is why
        /// nothing here rebuilds a request before its first submit: rebuilding
        /// would wipe the device_state the parked device left behind.
        io_outcome pump(parked_op& op, long& result) {
            for (;;) {
                const usize ask = static_cast<usize>(op.operation->request().length());
                long        n   = 0;
                if (op.operation->request().cancellation_requested() ||
                    !op.operation->request().begin_device()) {
                    result = -ECANCELED;
                    return io_outcome::completed;
                }
                if (op.socket_accept) {
                    const auto* socket = op.object->node()->unix_socket_capability();
                    n                  = socket == nullptr
                                             ? -EOPNOTSUPP
                                             : socket->accept(*op.object, op.operation->request());
                }
                else {
                    n = op.object->submit(op.operation->request());
                }
                if (op.control) {
                    if (n == 0 && !op.operation->request().complete()) {
                        op.operation->request().end_device();
                        return io_outcome::parked;
                    }
                    result = n;
                    op.operation->request().acknowledge();
                    op.operation->request().end_device();
                    return io_outcome::completed;
                }
                if (n == 0 && !op.operation->request().complete()) {
                    op.operation->request().end_device();
                    return io_outcome::parked; // a resume picks this request up
                }
                if (n < 0) {
                    // A stretch that already moved bytes is reported as the
                    // short operation it is; an error would throw them away.
                    result = op.moved != 0 ? static_cast<long>(op.moved) : n;
                    op.operation->request().acknowledge();
                    op.operation->request().end_device();
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
                    op.operation->request().acknowledge();
                    op.operation->request().end_device();
                    return io_outcome::completed;
                }
                // On to the next entry.  The file layer resolved the caller's
                // `offset_current` on the first submit and wrote the concrete
                // position back into the request, so continuing from the
                // request's own offset is what keeps a resume on the byte the
                // caller asked for.
                op.operation->request().offset(op.operation->request().offset() +
                                               consumed);
                const bool updates_position = op.operation->request().updates_position();
                const bool append_reserved  = op.operation->request().append_reserved();
                const bool has_append_length =
                    op.operation->request().has_append_length();
                const u32 append_length = op.operation->request().append_length();
                op.operation->request().end_device();
                op.operation->request().stretch(op.entries.window(),
                                                static_cast<u32>(op.entries.chunk()),
                                                op.operation->request().offset());
                op.operation->request().wake_handler_set(wake_request,
                                                         op.object->node().get());
                op.operation->request().updates_position(updates_position);
                if (has_append_length)
                    op.operation->request().append_length(append_length);
                op.operation->request().append_reserved(append_reserved);
            }
        }

        void complete_recvfrom_address(proc::process* owner, parked_op& op,
                                       long& result) {
            if (op.recvfrom_address && result >= 0) {
                astia_sockaddr address{};
                const auto*    socket = op.object->node()->unix_socket_capability();
                const auto     destination =
                    memory::user_ptr<astia_sockaddr>(op.recvfrom_address_user)
                        .validate_for(owner);
                if (socket == nullptr || !socket->recvfrom_address(*op.object, address) ||
                    !destination ||
                    !memory::copy_out_for(owner, destination.value.bytes(), &address))
                    result = -EFAULT;
            }
        }

        void resume_parked(proc::process* owner, astia_rings* ring, parked_op& op);

        bool publish_pending(astia_rings* ring, parked_op& op) {
            if (!op.pending_cqe)
                return true;
            if (op.token.canceled())
                op.pending_result = -ECANCELED;
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
                            const span<u8>* entries, u32 count,
                            ::astia::io::direction direction, bool control = false,
                            bool socket_accept = false, core::cltoken token = {}) {
            if (token.canceled())
                return cq_post(ring, s.user_data, -ECANCELED);
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
            p->type                  = parked_op::kind::operation;
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
            p->token                 = token;

            // Snapshot every entry for as long as this operation lasts.  From
            // here the device is handed kernel windows and never the user
            // pointers again, so a concurrent munmap — or a CoW split of a
            // buffer the operation is filling — cannot move or free what the
            // device is working on.
            for (u32 i = 0; i < count; ++i) {
                const long pinned =
                    memory::pin_user_span_for(slot.owner.get(), entries[i], direction,
                                              p->entries, slot.kernel_buffers);
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
            p->operation = new ::astia::io::operation(
                p->entries.window(), static_cast<u32>(p->entries.chunk()), s.offset,
                s.user_data, direction);
            if (p->operation == nullptr) {
                p->type = parked_op::kind::oom_completion;
                if (cq_post(ring, s.user_data, -ENOMEM))
                    p->done = true;
                else {
                    p->pending_cqe       = true;
                    p->pending_user_data = s.user_data;
                    p->pending_result    = -ENOMEM;
                }
                return true;
            }
            p->operation->heap_owned(true);
            if (!p->operation->request().park_device()) {
                p->done = true;
                if (!cq_post(ring, s.user_data, -EAGAIN)) {
                    p->pending_cqe       = true;
                    p->pending_user_data = s.user_data;
                    p->pending_result    = -EAGAIN;
                }
                return true;
            }
            p->generation = issue_request_generation();
            p->operation->request().generation(p->generation);
            p->operation->request().wake_handler_set(wake_request,
                                                     p->object->node().get());
            p->operation->request().append_length(static_cast<u32>(p->entries.bytes()));
            p->operation->request().updates_position(
                (s.op_flags & ASTIA_AIO_POSITIONED) == 0);
            // Capture before submit(): if readiness is reported while the
            // device is testing the request, publication below must notice it.
            const u64  generation = ready_generation();
            long       result     = 0;
            const auto outcome    = pump(*p, result);
            if (outcome == io_outcome::parked) {
                arm_request_timeout(*p);
                publish_waiter_hint(slot, park_index, p->object->node().get());
                if (ready_generation() != generation)
                    p->device_ready = true;
                return true; // parked: a device resume completes it
            }
            if (p->token.canceled())
                result = -ECANCELED;
            complete_recvfrom_address(slot.owner.get(), *p, result);
            // Satisfied inline.  The operation has already happened, so a
            // full CQ loses the completion rather than replaying it — the
            // CQ is sized to twice the SQ, so a wait is a bug, not a signal.
            if (p->operation->request().completion() != nullptr) {
                if (result < 0)
                    p->operation->request().completion()->fail_deferred(
                        error_from_errno(-result));
                else
                    p->operation->request().completion()->complete_deferred(
                        static_cast<i64>(result));
            }
            else if (!cq_post(ring, s.user_data, static_cast<i32>(result))) {
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
        void resume_parked(proc::process* owner, astia_rings* ring, parked_op& op) {
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
            if (op.operation == nullptr) {
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
                const i32 result =
                    op.token.canceled() ? -ECANCELED : static_cast<i32>(ready);
                if (op.operation->request().completion() != nullptr) {
                    op.operation->request().completion()->complete_deferred(
                        static_cast<i64>(result));
                    op.done = true;
                }
                else if (!cq_post(ring, op.operation->request().user_data(), result)) {
                    op.pending_cqe       = true;
                    op.pending_user_data = op.operation->request().user_data();
                    op.pending_result    = result;
                }
                else
                    op.done = true;
                op.resuming = false;
                return;
            }
            long result = 0;
            if (op.token.canceled()) {
                // Do not hand the device any more work for a cancelled
                // operation; retire the frozen request and publish the
                // cancellation through the normal settlement path.
                (void) op.operation->request().cancel_device(
                    false, core::cancel_reason::request);
                op.operation->request().acknowledge();
                result = -ECANCELED;
            }
            else if (pump(op, result) == io_outcome::parked) {
                arm_request_timeout(op);
                op.resuming = false;
                return;
            }
            complete_recvfrom_address(owner, op, result);
            // A full CQ leaves the operation and its shared file
            // reference parked for the next IRQ nudge.
            if (op.operation->request().completion() != nullptr) {
                if (result < 0)
                    op.operation->request().completion()->fail_deferred(
                        error_from_errno(-result));
                else
                    op.operation->request().completion()->complete_deferred(
                        static_cast<i64>(result));
                op.done = true;
            }
            else if (!cq_post(ring, op.operation->request().user_data(),
                              static_cast<i32>(result))) {
                op.pending_cqe       = true;
                op.pending_user_data = op.operation->request().user_data();
                op.pending_result    = static_cast<i32>(result);
            }
            else
                op.done = true;
            op.resuming = false;
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

        bool submit_io(ring_slot& slot, astia_rings* ring, const astia_sqe& s,
                       ::astia::io::direction direction, bool vectored,
                       core::cltoken token) {
            if (token.canceled())
                return ring::cq_post_guarded(ring, s.user_data, -ECANCELED);
            dispatch_context         context{slot, ring, s, proc::current_process()};
            mm::shptr<fs::open_file> object = file_for(context.current, s.fd);
            if (!object) {
                return context.complete(-EBADF);
            }
            const u32 access = object->flags() & fs::open_file_access_mask;
            if ((direction == ::astia::io::direction::output && access == 0 &&
                 (object->flags() & fs::pipe_write_endpoint_flag) == 0 &&
                 object->mode().is_regular()) ||
                (direction == ::astia::io::direction::input &&
                 access == ASTIA_AIO_O_WRONLY))
                return context.complete(-EBADF);
            // READ/WRITE name one range, READV/WRITEV up to max_segments of
            // them.  Both become the same list, so the engine has one path
            // and a device still gets one buffer per submit() either way.
            span<u8> ranges[mm::iovec::max_segments];
            u32      count = 0;
            if (!vectored) {
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
                    !memory::copy_in(
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
                const bool kernel_buffer =
                    context.slot.kernel_buffers && memory::kernel_span_ok(ranges[i]);
                if (!kernel_buffer &&
                    !memory::user_span_ok_for(context.slot.owner.get(), ranges[i])) {
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
                                  direction, false, false, token);
        }

    } // namespace detail

    mm::shptr<fs::open_file> file_for(proc::process* current, i32 fd) {
        return detail::file_for(current, fd);
    }

    mm::shptr<fs::open_file> file_for(i32 fd) {
        return detail::file_for(fd);
    }

    long resolve_at(proc::process* current, i32 raw_dirfd, const char* path, char* out,
                    usize capacity) {
        return detail::resolve_at(current, raw_dirfd, path, out, capacity);
    }

    bool parent_writable(proc::process* current, const char* path) {
        return detail::parent_writable(current, path);
    }

    bool submit_socket_request(ring::ring_slot& slot, astia_rings* ring,
                               const astia_sqe&                sqe,
                               const mm::shptr<fs::open_file>& object,
                               const span<u8>* entries, u32 count,
                               ::astia::io::direction direction, bool control,
                               bool socket_accept, core::cltoken token) {
        return detail::submit_request(slot, ring, sqe, object, entries, count, direction,
                                      control, socket_accept, token);
    }

    bool submit_io(ring::ring_slot& slot, astia_rings* ring, const astia_sqe& sqe,
                   ::astia::io::direction direction, bool vectored, core::cltoken token) {
        return detail::submit_io(slot, ring, sqe, direction, vectored, token);
    }

    bool publish_pending(astia_rings* ring, parked_op& op) {
        return detail::publish_pending(ring, op);
    }

    bool dispatch_one(ring_slot& slot, astia_rings* ring, const astia_sqe& sqe) {
        return registry::dispatch(slot, ring, sqe);
    }

    void retry_pending(ring_slot& slot, astia_rings* ring) {
        detail::retry_pending(slot, ring);
    }

    void resume_parked(proc::process* owner, astia_rings* ring, parked_op& op) {
        detail::resume_parked(owner, ring, op);
    }

    void complete_control(proc::process* owner, astia_rings* ring, parked_op& op,
                          i32 result) {
        detail::complete_control(owner, ring, op, result);
    }

    u64 poll_now_ns() {
        return detail::poll_now_ns();
    }

} // namespace astia::io::aio::dispatch
