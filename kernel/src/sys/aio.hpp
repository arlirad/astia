#pragma once

#include <astia/api/aio.h>
#include <astia/common.hpp>
#include <astia/fs.hpp>

// The kernel side of the async I/O ring ABI (<astia/api/aio.h>): the
// engine that consumes SQEs and posts CQEs.  Userspace will reach it
// through the SYS_IO_* syscalls; in-kernel clients (today: the uart16550
// driver and the boot selftest) call these functions directly — same
// rings, same SQE/CQE structs, same cursor ordering, minus the trap.
//
// v1 scope: rings carved from a static pool, VFS-backed file descriptors,
// and NOP/READ/WRITE/OPENAT/CLOSE/DUP/DUP2/STATX.  A READ/WRITE the device
// cannot satisfy at once parks at the device's own submit() stage; the
// device later calls io_device_ready() (from its IRQ handler, say) and the
// engine resumes that request.  That is the whole point of the shape:
// io_enter never spins inside a device — the only waiting it does is on
// completions, and that wait sleeps on hlt until an interrupt wakes it.
//
// Userspace: the pool lives in kernel .bss, which the higher half maps
// supervisor-only, so io_setup for a *process* additionally aliases the
// ring's frames into that process' user half and hands back the user
// address.  In-kernel clients (no address space) receive the kernel
// address.  Both are accepted by io_enter/io_destroy and address the same
// frames, so the cursor protocol is identical either way; io_destroy
// drops the alias along with the slot.
//
// Concurrency: one producer and one consumer per ring, and the engine is
// the single CQE producer — dispatch (task context) and device-ready
// (IRQ context) serialize on the interrupt gate, not on a lock.  With
// threads, callers will need the same external serialization the libc
// side documents (libc/src/file.c); nothing here takes a lock today.

namespace astia::proc {
    class process;
}

namespace astia::sys {

    /// Pool geometry for kernel-owned rings (v1).  The ABI allows
    /// entries; the static pool serves 64, which is more than any
    /// in-kernel client submits — userspace rings will move to
    /// heap-backed mappings when the syscall layer lands.
    inline constexpr u32 kernel_ring_max_entries = 64;
    static_assert(kernel_ring_max_entries <= IO_MAX_ENTRIES,
                  "kernel ring cap must fit the AIO ABI cap");

    /// The object the engine invokes for a descriptor.  Device classes
    /// derive from this through fs::file.
    using io_device = fs::file;

    /// Carve a ring out of the kernel pool: entries rounded up to a
    /// power of two within [1, kernel_ring_max_entries], CQ sized
    /// max(64, 2 * sq) per the ABI.  flags takes the SETUP_* set; none
    /// are implemented yet, so 0 only.  Returns >= 0, or -errno:
    /// -EINVAL (bad flags/entries), -ENFILE (pool exhausted), and — for a
    /// caller with an address space — -ESRCH/-ENOMEM when the user-half
    /// alias of the ring could not be mapped.
    long io_setup(u32 entries, u32 flags, astia_rings** out);

    /// Release a ring back to the pool.  Ops still parked on it refuse
    /// the teardown (-EBUSY) so no completion can be orphaned.
    long io_destroy(astia_rings* ring);

    /// Submit up to to_submit SQEs from the shared tail and, when
    /// min_complete > 0, wait until that many completions are visible
    /// (unreaped).  flags takes ENTER_GETEVENTS (redundant with
    /// min_complete, accepted for ABI shape) and ENTER_ABS_TIMEOUT
    /// (timeout_ticks is then an absolute tick deadline).  A relative
    /// timeout of 0 waits unbounded.
    ///
    /// Returns the number of SQEs consumed, or -errno:
    ///   -EINVAL    unknown flags / not a live ring
    ///   -EAGAIN    a wait was asked for with the interrupt gate closed:
    ///              the kernel cannot sleep for a completion then (kmain
    ///              opens the gate before waiting; the syscall layer
    ///              will manage this on behalf of userspace)
    ///   -ETIMEDOUT the deadline passed; submissions that happened are
    ///              not undone
    long io_enter(astia_rings* ring, u32 to_submit, u32 min_complete, u32 flags,
                  u64 timeout_ticks);

    /// SYS_IO_ENTER shape of the same call: the ABI hands a userspace
    /// astia_timespec* (NULL = wait unbounded), the engine speaks tick
    /// deadlines.  Nanoseconds become ticks here — rounded up, and never
    /// down to 0, because 0 is io_enter's "no deadline" sentinel — with
    /// ENTER_ABS_TIMEOUT read as an absolute monotonic deadline (ticks
    /// and CLOCK_MONOTONIC share the boot origin).  Returns what
    /// io_enter returns, or -EIO/-EINVAL for a clockless arch or a
    /// malformed timespec.
    long io_enter_abi(astia_rings* ring, u32 to_submit, u32 min_complete, u32 flags,
                      const astia_timespec* timeout);

    /// Device-side nudge: "this fd may have data now."  The engine
    /// retries every op parked on the fd and posts the completions that
    /// result.  Safe from IRQ context — that is the intended caller.
    void io_device_ready(i32 fd);

    /// Wake parked reads on every open descriptor backed by this device
    /// file instance.  Device IRQ handlers should use this so
    /// dynamically opened `/dev/*` descriptors wake just like stdio.
    void io_device_ready_context(const io_device* dev);
    void io_device_ready_context_except(const io_device* dev, u64 user_data);
    void io_exec_process(proc::process* owner);
    void io_exit_process(proc::process* owner);

} // namespace astia::sys
