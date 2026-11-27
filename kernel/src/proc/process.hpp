#pragma once

#include <astia/api/syscalls.h>
#include <astia/arch/spinlock_guard.hpp>
#include <astia/atomic.hpp>
#include <astia/common.hpp>
#include <astia/core/cancellation.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/core/task.hpp>
#include <astia/dbg/panic_report.hpp>
#include <astia/mm/address_space.hpp>
#include <astia/mm/iovec.hpp>
#include <astia/mm/shptr.hpp>
#include <astia/mm/uptr.hpp>
#include <astia/proc/user_layout.hpp>
#include <astia/resource_limits.hpp>
#include <astia/span.hpp>
#include <astia/wait.hpp>

#include "../ipc/signal.hpp"
#include "cwd_context.hpp"
#include "fd_table.hpp"
#include "mmap_region.hpp"
#include "signal_waiter_registry.hpp"
#include "thread.hpp"

namespace astia::proc {
    struct exec_result {
        u64 entry = 0;
        u64 stack = 0;
    };

    inline constexpr u32 max_processes = resource_limits::policy::process_count;
    inline constexpr u32 max_threads_per_process =
        resource_limits::policy::threads_per_process;
    inline constexpr u32 max_waiters_per_process =
        resource_limits::policy::process_waiters;
    inline constexpr u32 max_signal_waiters_per_process = signal_waiter_capacity;
    inline constexpr u32 max_supplementary_groups =
        resource_limits::policy::supplementary_groups;

    /// Userspace spawn limits for the startup frame (argv/envp).
    inline constexpr u32 max_argv_entries = resource_limits::policy::argv_entries;
    inline constexpr u32 max_env_entries  = resource_limits::policy::env_entries;
    inline constexpr usz max_spawn_string_bytes =
        resource_limits::policy::spawn_string_bytes;

    enum class process_state : u8 {
        fresh,
        runnable,
        stopping,
        stopped,
        zombie,
        dead,
    };

    /// Process-owned lifetime and descriptor boundary.  The process
    /// owns thread references and open-file descriptions; threads keep
    /// a non-owning backlink to their process.
    class process {
        public:
        inline static constexpr u32 max_supplementary_groups = 16;

        process(pid_t id, pid_t parent, const char* name);
        ~process();

        pid_t pid() const {
            return _pid;
        }

        pid_t parent_pid() const {
            return _parent_pid;
        }

        pgrp_t process_group() const {
            arch::interrupt_spinlock_guard guard(_metadata_lock);

            return _process_group;
        }

        pid_t session() const {
            arch::interrupt_spinlock_guard guard(_metadata_lock);

            return _session;
        }

        bool execed() const {
            arch::interrupt_spinlock_guard guard(_metadata_lock);

            return _execed;
        }

        void mark_execed() {
            arch::interrupt_spinlock_guard guard(_metadata_lock);

            _execed = true;
        }

        u32 uid() const {
            arch::interrupt_spinlock_guard guard(_metadata_lock);

            return _uid;
        }

        u32 euid() const {
            arch::interrupt_spinlock_guard guard(_metadata_lock);

            return _euid;
        }

        u32 gid() const {
            arch::interrupt_spinlock_guard guard(_metadata_lock);

            return _gid;
        }

        u32 egid() const {
            arch::interrupt_spinlock_guard guard(_metadata_lock);

            return _egid;
        }

        u32 umask() const {
            arch::interrupt_spinlock_guard guard(_metadata_lock);

            return _umask;
        }

        u32 set_umask(u32 value) {
            arch::interrupt_spinlock_guard guard(_metadata_lock);

            const u32 old = _umask;

            _umask = value & 0777u;

            return old;
        }

        i64  setuid(u32);
        i64  seteuid(u32);
        i64  setgid(u32);
        i64  setegid(u32);
        i64  setreuid(u32, u32);
        i64  setregid(u32, u32);
        i64  getgroups(i32 count, u32* out) const;
        i64  setgroups(i32 count, const u32* values);
        bool in_group(gid_t group) const;

        bool can_access(fs::file_mode mode, uid_t owner, gid_t group,
                        fs::permission_bits want) const {
            arch::interrupt_spinlock_guard guard(_metadata_lock);

            if (_euid == 0)
                return want != fs::permission_bits::execute ||
                       mode.owner_allows(fs::permission_bits::execute) ||
                       mode.group_allows(fs::permission_bits::execute) ||
                       mode.other_allows(fs::permission_bits::execute);

            return _euid == static_cast<u32>(owner) ? mode.owner_allows(want) : ([&] {
                if (_egid == static_cast<u32>(group))
                    return mode.group_allows(want);

                for (u32 i = 0; i < _group_count; ++i) {
                    if (_groups[i] == group)
                        return mode.group_allows(want);
                }

                return mode.other_allows(want);
            }());
        }

        bool can_search(fs::file_mode mode, uid_t owner, gid_t group) const {
            if (!mode.is_directory())
                return false;

            if (euid() == 0)
                return true;

            return can_access(mode, owner, group, fs::permission_bits::execute);
        }

        core::task<i64> change_directory(const char* path);
        i64             change_directory(const fs::open_file& directory);
        i64             copy_cwd(span<char> buffer) const;

        i64 copy_cwd(char* buffer, usz length) const {
            return copy_cwd(span<char>(buffer, length));
        }

        i64 copy_root(span<char> buffer) const;

        i64 copy_root(char* buffer, usz length) const {
            return copy_root(span<char>(buffer, length));
        }

        // Process helpers compose cwd/dirfd-relative paths; fs::resolve_path()
        // performs canonical VFS resolution as the second layer.
        i64 compose_path(const char* path, span<char> buffer) const;

        i64 compose_path(const char* path, char* buffer, usz length) const {
            return compose_path(path, span<char>(buffer, length));
        }

        i64 compose_path_at(fd_t dirfd, const char* path, span<char> buffer) const;

        i64 compose_path_at(fd_t dirfd, const char* path, char* buffer,
                            usz length) const {
            return compose_path_at(dirfd, path, span<char>(buffer, length));
        }

        /// Resolve a path through this process's filesystem root and cwd.
        /// Absolute paths start at the root; relative paths resolve from cwd
        /// and fail with -ENOENT while cwd is outside the root.
        i64 resolve_path(const char* path, span<char> buffer, bool follow_final) const;

        i64 resolve_path(const char* path, char* buffer, usz length,
                         bool follow_final) const {
            return resolve_path(path, span<char>(buffer, length), follow_final);
        }

        /// Resolve a path relative to dirfd, rejecting a directory descriptor
        /// whose canonical path is outside this process's root.
        i64 resolve_path_at(fd_t dirfd, const char* path, span<char> buffer,
                            bool follow_final) const;

        i64 resolve_path_at(fd_t dirfd, const char* path, char* buffer, usz length,
                            bool follow_final) const {
            return resolve_path_at(dirfd, path, span<char>(buffer, length), follow_final);
        }

        bool path_within_root(const char* path) const {
            return _cwd.within_root(path);
        }

        /// Make `path` the process filesystem root.  Only euid 0 may call it;
        /// the path is resolved through the current root first, so a process
        /// can only move its root deeper, never back out.
        i64 chroot(const char* path);

        void inherit_groups(const process& parent) {
            _cwd.inherit_from(parent._cwd);

            arch::interrupt_spinlock_guard parent_guard(parent._metadata_lock);
            arch::interrupt_spinlock_guard guard(_metadata_lock);

            if (parent._pid == pid_t{}) {
                _process_group = pgrp_from_raw(raw_id(_pid));
                _session       = _pid;
            }
            else {
                _process_group = parent._process_group;
                _session       = parent._session;
            }

            _controlling_terminal = parent._controlling_terminal;
            _uid                  = parent._uid;
            _euid                 = parent._euid;
            _gid                  = parent._gid;
            _egid                 = parent._egid;
            _group_count          = parent._group_count;

            for (u32 i = 0; i < _group_count; ++i) {
                _groups[i] = parent._groups[i];
            }

            _rlimit_nofile = parent._rlimit_nofile;
            _rlimit_as     = parent._rlimit_as;
            _umask         = parent._umask;
        }

        void process_group(pgrp_t value) {
            arch::interrupt_spinlock_guard guard(_metadata_lock);

            _process_group = value;
        }

        void session(pid_t value) {
            arch::interrupt_spinlock_guard guard(_metadata_lock);

            _session = value;
        }

        mm::shptr<fs::open_file> controlling_terminal() const {
            arch::interrupt_spinlock_guard guard(_metadata_lock);

            return _controlling_terminal;
        }

        void controlling_terminal(const mm::shptr<fs::open_file>& value) {
            arch::interrupt_spinlock_guard guard(_metadata_lock);

            _controlling_terminal = value;
        }

        bool claim_controlling_terminal(const mm::shptr<fs::open_file>& value) {
            arch::interrupt_spinlock_guard guard(_metadata_lock);

            if (!value || _pid != _session || _controlling_terminal)
                return false;

            _controlling_terminal = value;

            return true;
        }

        const char* name() const {
            return _name;
        }

        process_state state() const {
            return static_cast<process_state>(
                astia::atomic_ref<u8>(_state).load(astia::memory_order::acquire));
        }

        void state(process_state value) {
            astia::atomic_ref<u8>(_state).store(static_cast<u8>(value),
                                                astia::memory_order::release);
        }

        i32 exit_status() const {
            return _exit_status;
        }

        i32 wait_status() const {
            return _termination_signal != 0 ? wait_status_from_signal(_termination_signal)
                                            : wait_status_from_exit(_exit_status);
        }

        i32 stop_status() const {
            return wait_status_from_stop(_stop_signal);
        }

        bool stop_event_pending() const {
            return astia::atomic_ref<u8>(_stop_event)
                       .load(astia::memory_order::acquire) != 0;
        }

        bool continued_event_pending() const {
            return astia::atomic_ref<u8>(_continued_event)
                       .load(astia::memory_order::acquire) != 0;
        }

        void clear_stop_event() {
            astia::atomic_ref<u8>(_stop_event).store(0, astia::memory_order::release);
        }

        void clear_continued_event() {
            astia::atomic_ref<u8>(_continued_event)
                .store(0, astia::memory_order::release);
        }

        void signal_state_change(signal_number signal);
        void interrupt_signal_io(signal_number signal);
        void request_kill();
        i64  alarm(u32 seconds, u64 now, u32 hz);
        void expire_alarm(u64 now);

        void signal_termination(signal_number signal) {
            _termination_signal = static_cast<i32>(signal);
        }

        signal_number termination_signal() const {
            return static_cast<signal_number>(_termination_signal);
        }

        /// Process-wide cancellation root.  Cancelling it stops every token
        /// created beneath it, across every subsystem that opts in.
        core::clsource& cancellation_root() {
            return _cancellation_root;
        }

        const core::clsource& cancellation_root() const {
            return _cancellation_root;
        }

        signal_state& signals() {
            return _signals;
        }

        const signal_state& signals() const {
            return _signals;
        }

        /// Called by a thread that is exiting.  When the last live
        /// thread is gone the process becomes a zombie carrying `status`
        /// until its parent reaps it with proc::waitpid().
        bool thread_exited(thread* value, i32 status);
        void request_exit(thread* caller, i32 status);
        void terminate_for_signal(signal_number signal, thread* caller);

        bool process_exit_requested() const {
            return astia::atomic_ref<u8>(_exit_requested)
                       .load(astia::memory_order::acquire) != 0;
        }

        i32 requested_exit_status() const {
            return astia::atomic_ref<i32>(_requested_exit_status)
                .load(astia::memory_order::acquire);
        }

        /// Move the process's owning reference to a dead thread out of
        /// the thread table.  Used to defer destruction: the dying
        /// thread still runs on its own kernel stack.
        mm::shptr<thread> take_thread(thread* value);
        /// Retain a process-owned thread while an external subsystem holds it.
        mm::shptr<thread> retain_thread(thread* value);
        /// Drop the process's owning reference to a thread.  Only safe
        /// when the target can no longer run (joined or deferred).
        void release_thread(thread* value);
        void release_thread_stack(thread* value);
        void release_thread_tls(thread* value);
        /// Threads blocked in proc::waitpid() for one of our children.
        bool add_waiter(thread* value);
        void remove_waiter(thread* value);
        void wake_waiters();
        bool add_signal_waiter(thread* value);
        bool add_signal_waiter(thread* value, bool timed, u64 deadline);
        /// Arm a signal wait and park the thread in one step.  Returns false
        /// when a wake is already pending, so the caller re-scans instead.
        bool arm_signal_waiter(thread* value, std::coroutine_handle<> continuation);
        void remove_signal_waiter(thread* value);
        bool signal_waiter_timed_out(thread* value);
        void expire_signal_waiters(u64 now);
        void wake_signal_waiters();

        bool              add_thread(const mm::shptr<thread>& value);
        mm::shptr<thread> thread_at(u32 index) const;

        u32 thread_count() const {
            return _thread_count;
        }

        u32 live_thread_count() {
            arch::interrupt_spinlock_guard guard(_lifecycle_lock);

            u32 count = 0;

            for (u32 i = 0; i < max_threads_per_process; ++i) {
                if (_threads[i] && _threads[i]->state() != thread_state::dead)
                    ++count;
            }

            return count;
        }

        bool                     install_standard_streams();
        mm::shptr<fs::open_file> descriptor(fd_t fd) const;
        bool set_descriptor(fd_t fd, const mm::shptr<fs::open_file>& value);
        bool set_descriptor(fd_t fd, const mm::shptr<fs::open_file>& value, u32 flags);
        optional<fd_t> allocate_descriptor(const mm::shptr<fs::open_file>& value,
                                           u32                             flags = 0);
        bool           clear_descriptor(fd_t fd);
        void           close_on_exec_descriptors();
        optional<fd_t> duplicate_descriptor(fd_t old_fd);
        optional<fd_t> duplicate_descriptor(fd_t old_fd, fd_t minimum, u32 flags);
        u32            descriptor_flags(fd_t fd) const;
        bool           set_descriptor_flags(fd_t fd, u32 flags);
        optional<fd_t> duplicate_descriptor_to(fd_t old_fd, fd_t new_fd);

        /// Load an ET_EXEC/ET_DYN image with the shared ELF loader into
        /// this process address space.  This prepares the image only;
        /// user entry/TSS return is an architecture step after this.
        bool load_user_image(span<const u8> bytes, u64* entry_out);

        bool load_user_image(const void* bytes, usz length, u64* entry_out) {
            return load_user_image(span<const u8>(static_cast<const u8*>(bytes), length),
                                   entry_out);
        }

        /// x86_64 thread pointer (%fs base) for this process's static
        /// TLS block; 0 when the image declares no PT_TLS.
        u64 tls_tp() const {
            return _tls_tp;
        }

        /// Startup-vector facts about the image this process will enter.
        /// Carried across replace_image and copied by fork.
        struct exec_image_info {
            u64 base   = 0; ///< payload load bias (AT_ASTIA_PAYLOAD_BASE)
            u64 entry  = 0; ///< payload entry (AT_ENTRY)
            u64 phdr   = 0; ///< payload phdr table (AT_PHDR)
            u64 interp = 0; ///< interpreter load bias (AT_BASE), 0 = static
            u16 phnum  = 0;
            u16 phent  = 0;
            /// Static TLS arena: payload block start, thread pointer, and the
            /// bottom the interpreter may lay shared-object blocks out in.
            u64 tls_base  = 0;
            u64 tls_tp    = 0;
            u64 tls_limit = 0;
        };

        const exec_image_info& exec_image() const {
            return _exec_image;
        }

        /// Cancellation token for I/O this process starts (dlmap image
        /// reads, file mappings).  Execve retires it with the old image.
        core::cltoken io_token() {
            return _cancellation_root.token();
        }

        /// Map an already-backed physical range at an exact address.  The
        /// caller owns a `share_range` reference on `region` covering
        /// `region_page..+pages`; this mapping adopts it.  `cow` installs the
        /// page table entries read-only (the VMA stays writable) so the first
        /// store faults into a private copy.
        bool map_shared_range(u64 base, usz pages, map_protection prot,
                              mm::phys_region* region, u32 region_page, bool cow);
        /// Create the process's first user thread.  Builds the
        /// userspace startup frame (argc/argv/envp, arch-neutral
        /// content) on the freshly mapped user stack and enters at
        /// `entry` with the computed stack pointer.  argv/envp may be
        /// null; an empty argv defaults to argv[0] = process name.
        mm::shptr<thread> create_user_thread(u64 entry, char* const argv[] = nullptr,
                                             char* const envp[] = nullptr);
        /// Create an additional user thread in this address space.  The
        /// supplied stack belongs to userspace (pthread_create maps it);
        /// `argument` becomes the new entry's first ABI argument.
        mm::shptr<thread> create_thread(u64 entry, u64 user_stack_top, u64 argument,
                                        u64 argument1 = 0, bool owns_user_stack = false,
                                        u64 user_stack_base = 0, usz user_stack_size = 0);

        struct thread_stack_snapshot {
            u64  stack_base = 0;
            usz  stack_size = 0;
            bool detached   = false;
            bool supported  = false;
        };

        mm::shptr<thread> thread_stack_info(tid_t id, thread_stack_snapshot& snapshot);
        /// Find a thread by its process-local tid, retaining it while a
        /// blocking join syscall operates on it.
        mm::shptr<thread> thread_by_tid(tid_t id);

        const mm::address_space* address_space() const {
            return _address_space.attached() ? &_address_space : nullptr;
        }

        const mm::address_space& memory_space() const {
            return _address_space;
        }

        mm::address_space* address_space() {
            return _address_space.attached() ? &_address_space : nullptr;
        }

        /// Mutable process address space for mappings owned by this process.
        mm::address_space& memory_space() {
            return _address_space;
        }

        /// mmap: anonymous private memory; `addr` is a placement hint
        /// (0 = the kernel picks the highest hole below the mmap
        /// ceiling).  Returns the mapped base, or a negated errno.
        u64 map_anonymous(vaddr_t addr, bytes_t len, map_protection prot,
                          map_flags flags);

        /// mmap of an open file, private (snapshot) semantics: the
        /// file's bytes are captured into fresh frames at map time,
        /// writes land on the private copy, and the mapping keeps the
        /// open file description alive across close().  `fd` resolves
        /// in this process' table; `off` must be page-aligned.
        core::task<u64> map_file(fd_t fd, vaddr_t addr, bytes_t len, map_protection prot,
                                 map_flags flags, offset_t off);
        u64 map_file_sync(fd_t fd, vaddr_t addr, bytes_t len, map_protection prot,
                          map_flags flags, offset_t off);

        /// munmap: drop [addr, addr + len) rounded up to pages; parts
        /// of mapped regions outside the range are kept.  Returns 0,
        /// or a negated errno.  Unmapping never-mapped space succeeds.
        i32 unmap(u64 addr, usz len);

        i32 protect(u64 addr, usz len, map_protection prot);

        core::task<i32> msync(u64 addr, usz len, u32 flags);
        i32             msync_sync(u64 addr, usz len, u32 flags);

        /// The mmap_region covering `addr`, or nullptr — the entry
        /// point for the future user page-fault path.
        mmap_region* find_region(u64 addr);

        i32 resolve_shared_futex(u64 addr, shared_futex_mapping& out);

        /// Snapshot [addr, addr + len) into `out` as one more entry of an
        /// asynchronous operation's buffer list: 0, or a negated errno.
        /// `destination` marks a buffer the operation will fill (see
        /// mm::pinned_buffer).  A range that spans adjacent mappings is
        /// pinned as one entry per backing region: pieces that continue the
        /// same region are coalesced, so an ordinary buffer crossing a DSO's
        /// page-granular VMAs still uses a single entry.  A process
        /// with no address space should not be here at all: its pointers are
        /// kernel memory, which cannot be unmapped out from under the
        /// operation in the first place (see sys::pin_user_span).
        i32 pin_user(u64 addr, usz len, bool destination, mm::iovec& out);

        /// Copy-on-write fork support (called on the parent): build
        /// `child`'s address space from this one.  Every VMA's backing
        /// gains one reference, private writable pages lose their
        /// write bits on BOTH sides, and the child's PTEs start
        /// read-only — the first write anywhere faults into a private
        /// copy.  _tls_tp is copied as well.
        core::task<bool> clone_address_space_cow(process* child);

        /// Share this process's open file descriptions with a fork
        /// child: the child's entries reference the SAME descriptions
        /// (POSIX — file offsets move together).
        bool copy_descriptors_from(const process& other);

        i64 get_rlimit(i32 resource, astia_rlimit* out) const;
        i64 set_rlimit(i32 resource, const astia_rlimit& value);

        /// Capture read-mostly process metadata without taking lifecycle or
        /// wait locks.  The lock also makes credential/group arrays and the
        /// tty pointer a coherent snapshot for procfs-like readers.
        struct metadata_snapshot {
            pgrp_t                   process_group;
            pid_t                    session;
            u32                      uid, gid;
            mm::shptr<fs::open_file> controlling_terminal;
        };

        /// Lock-free best-effort snapshot for the panic report.  Metadata is
        /// reported as unavailable when the lock is already held.
        void panic_snapshot(dbg::panic_process& out) const;

        void snapshot_metadata(metadata_snapshot& out) const {
            arch::interrupt_spinlock_guard guard(_metadata_lock);

            out.process_group        = _process_group;
            out.session              = _session;
            out.uid                  = _uid;
            out.gid                  = _gid;
            out.controlling_terminal = _controlling_terminal;
        }

        bool exec_user_image(span<const u8> bytes, char* const argv[], char* const envp[],
                             u64* entry_out, u64* stack_out);

        bool exec_user_image(const void* bytes, usz length, char* const argv[],
                             char* const envp[], u64* entry_out, u64* stack_out) {
            return exec_user_image(span<const u8>(static_cast<const u8*>(bytes), length),
                                   argv, envp, entry_out, stack_out);
        }

        core::task<exec_result> exec_user_file_async(mm::shptr<fs::open_file> file,
                                                     usz length, char* const argv[],
                                                     char* const envp[]);
        core::task<bool> load_user_file_async(mm::shptr<fs::open_file> file, usz length,
                                              u64* entry_out);

        /// Fork: the child's first thread, resuming `uctx` inside the
        /// parent's ring-3 call site with rax = 0.
        mm::shptr<thread> create_fork_thread(const arch::user_context& uctx,
                                             signal_mask               blocked_signals,
                                             u64 user_stack_base = 0,
                                             usz user_stack_size = 0);

        /// Resolve a ring-3 write fault: a uniquely-held CoW page
        /// regains its write bit, otherwise the page is copied out and
        /// the VMA splits around it.  True when the faulting
        /// instruction will now succeed.
        bool cow_fault(u64 addr, bool write);

        private:
        void notify_parent_of_state_change(signal_info info = {});

        /// File the VMA entry for an already-mapped range [base, base
        /// + pages): `region` adopts the `pages` contiguous frames the
        /// caller mapped PTEs for, and the entry takes over its
        /// reference.
        bool register_mapping(u64 base, usz pages, map_protection prot,
                              mm::phys_region* region);
        bool build_user_stack(char* const argv[], char* const envp[], u64* rsp_out);
        void discard_image();
        void rollback_tls_failure(void* image, mm::address_space& map);
        void reset_signal_stacks_for_exec();
        bool clone_tls_for_thread(tid_t id, u64& fs_base, u64& mapping_base,
                                  usz& mapping_pages);
        void replace_image(process& staging);
        /// Reserve an address, install PTEs without _mmap_lock, then publish
        /// the VMA.  On failure the reservation and PTEs are rolled back and
        /// `region` stays the caller's.
        bool install_mapping(u64 hint, usz pages, map_protection prot,
                             mm::phys_region*                region,
                             const mm::shptr<fs::open_file>& file, u64 file_offset,
                             u64& base_out, usz region_page = 0, bool shared = false,
                             bool stack = false, u64 fixed_base = 0, bool cow = false);

        bool   grow_stack(mmap_region* vma, u64 addr, bool write);
        bool   materialize_lazy_page(mmap_region* vma, u64 virt, usz backing_page,
                                     bool write);
        pid_t  _pid;
        pid_t  _parent_pid;
        pgrp_t _process_group;
        pid_t  _session;
        bool   _execed = false;
        mm::shptr<fs::open_file> _controlling_terminal;
        u32                      _uid = 0, _euid = 0, _gid = 0, _egid = 0;
        u32                      _umask = 0;
        gid_t                    _groups[max_supplementary_groups]{};
        u32                      _group_count = 0;
        char                     _name[32]{};
        // These are observed by registry, scheduler, and wait paths that do
        // not hold _lifecycle_lock. Keep the byte-sized representation
        // and publish transitions explicitly rather than widening the record.
        mutable u8   _state                 = static_cast<u8>(process_state::fresh);
        i32          _exit_status           = 0;
        i32          _termination_signal    = 0;
        i32          _stop_signal           = 0;
        mutable u8   _stop_event            = 0;
        mutable u8   _continued_event       = 0;
        mutable u8   _exit_requested        = 0;
        mutable i32  _requested_exit_status = 0;
        signal_state _signals{};
        u64          _alarm_deadline = 0;

        mm::shptr<thread>      _threads[max_threads_per_process];
        u32                    _thread_count = 0;
        thread*                _waiters[max_waiters_per_process]{};
        signal_waiter_registry _signal_waiters{};
        fd_table               _descriptors;
        mm::address_space      _address_space;
        u64                    _tls_tp = 0;
        exec_image_info        _exec_image{};
        astia_rlimit           _rlimit_nofile{max_process_fds, max_process_fds};
        astia_rlimit           _rlimit_as{~u64{0}, ~u64{0}};
        u64                    _mapped_bytes = 0;
        cwd_context            _cwd{};
        mmap_region*           _mmap_regions = nullptr; // base-sorted
        core::clsource         _cancellation_root{};
        // Lifecycle/thread state is process-local; keeping this lock on the
        // object avoids serializing unrelated processes.
        core::spinlock _lifecycle_lock{};
        // Credentials, groups, session/job-control metadata, and rlimits are
        // read frequently but are independent of thread lifetime and waits.
        mutable core::spinlock _metadata_lock{};
        core::spinlock         _mmap_lock{};
        core::spinlock         _clone_gate{};
    };

    /// Queue a just-died thread for destruction once its kernel stack
    /// is no longer executing anywhere.  Reaped by reap_dead_threads()
    /// after the next scheduler context switch.
    void defer_dead_thread(const mm::shptr<thread>& value);
    /// Destroy queued dead threads.  Called from the scheduler only
    /// after switching away from a context, where no queued thread can
    /// still be running on its own stack.
    void reap_dead_threads();
    bool defer_dead_process(const mm::shptr<process>& value);
    void reap_dead_processes();
} // namespace astia::proc
