#pragma once

#include "../ipc/signal.hpp"
#include "cwd_context.hpp"
#include "fd_table.hpp"
#include "mmap_region.hpp"
#include "signal_waiter_registry.hpp"
#include "thread.hpp"
#include "user_layout.hpp"
#include <astia/api/syscalls.h>
#include <astia/common.hpp>
#include <astia/core/task.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/mm/iovec.hpp>
#include <astia/mm/address_space.hpp>
#include <astia/mm/shptr.hpp>
#include <astia/mm/uptr.hpp>
#include <astia/resource_limits.hpp>
#include <astia/span.hpp>
#include <astia/wait.hpp>

namespace astia::proc {

    struct exec_result {
        u64 entry = 0;
        u64 stack = 0;
    };

    inline constexpr u32 max_processes                  = resource_limits::policy::process_count;
    inline constexpr u32 max_threads_per_process        = resource_limits::policy::threads_per_process;
    inline constexpr u32 max_waiters_per_process        = resource_limits::policy::process_waiters;
    inline constexpr u32 max_signal_waiters_per_process = signal_waiter_capacity;
    inline constexpr u32 max_supplementary_groups       = resource_limits::policy::supplementary_groups;

    /// Userspace spawn limits for the startup frame (argv/envp).
    inline constexpr u32   max_argv_entries       = resource_limits::policy::argv_entries;
    inline constexpr u32   max_env_entries        = resource_limits::policy::env_entries;
    inline constexpr usize max_spawn_string_bytes = resource_limits::policy::spawn_string_bytes;

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
            return pid_;
        }
        pid_t parent_pid() const {
            return parent_pid_;
        }
        pgrp_t process_group() const {
            arch::interrupt_spinlock_guard guard(metadata_lock_);
            return process_group_;
        }
        pid_t session() const {
            arch::interrupt_spinlock_guard guard(metadata_lock_);
            return session_;
        }
        u32 uid() const {
            arch::interrupt_spinlock_guard guard(metadata_lock_);
            return uid_;
        }
        u32 euid() const {
            arch::interrupt_spinlock_guard guard(metadata_lock_);
            return euid_;
        }
        u32 gid() const {
            arch::interrupt_spinlock_guard guard(metadata_lock_);
            return gid_;
        }
        u32 egid() const {
            arch::interrupt_spinlock_guard guard(metadata_lock_);
            return egid_;
        }
        u32 umask() const {
            arch::interrupt_spinlock_guard guard(metadata_lock_);
            return umask_;
        }
        u32 set_umask(u32 value) {
            arch::interrupt_spinlock_guard guard(metadata_lock_);
            const u32 old = umask_;
            umask_ = value & 0777u;
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
            arch::interrupt_spinlock_guard guard(metadata_lock_);
            if (euid_ == 0)
                return want != fs::permission_bits::execute ||
                       mode.owner_allows(fs::permission_bits::execute) ||
                       mode.group_allows(fs::permission_bits::execute) ||
                       mode.other_allows(fs::permission_bits::execute);
            return euid_ == static_cast<u32>(owner) ? mode.owner_allows(want) : ([&] {
                if (egid_ == static_cast<u32>(group))
                    return mode.group_allows(want);
                for (u32 i = 0; i < group_count_; ++i)
                    if (groups_[i] == group)
                        return mode.group_allows(want);
                return mode.other_allows(want);
            }());
        }
        i64 change_directory(const char* path);
        i64 copy_cwd(span<char> buffer) const;
        i64 copy_cwd(char* buffer, usize length) const {
            return copy_cwd(span<char>(buffer, length));
        }
        // Process helpers compose cwd/dirfd-relative paths; fs::resolve_path()
        // performs canonical VFS resolution as the second layer.
        i64 compose_path(const char* path, span<char> buffer) const;
        i64 compose_path(const char* path, char* buffer, usize length) const {
            return compose_path(path, span<char>(buffer, length));
        }
        i64 compose_path_at(fd_t dirfd, const char* path, span<char> buffer) const;
        i64 compose_path_at(fd_t dirfd, const char* path, char* buffer,
                            usize length) const {
            return compose_path_at(dirfd, path, span<char>(buffer, length));
        }
        void inherit_groups(const process& parent) {
            cwd_.inherit_from(parent.cwd_);
            arch::interrupt_spinlock_guard parent_guard(parent.metadata_lock_);
            arch::interrupt_spinlock_guard guard(metadata_lock_);
            if (parent.pid_ == pid_t{}) {
                process_group_ = pgrp_from_raw(raw_id(pid_));
                session_       = pid_;
            }
            else {
                process_group_ = parent.process_group_;
                session_       = parent.session_;
            }
            controlling_terminal_ = parent.controlling_terminal_;
            uid_         = parent.uid_;
            euid_        = parent.euid_;
            gid_         = parent.gid_;
            egid_        = parent.egid_;
            group_count_ = parent.group_count_;
            for (u32 i = 0; i < group_count_; ++i)
                groups_[i] = parent.groups_[i];
            rlimit_nofile_ = parent.rlimit_nofile_;
            rlimit_as_     = parent.rlimit_as_;
            umask_         = parent.umask_;
        }
        void process_group(pgrp_t value) {
            arch::interrupt_spinlock_guard guard(metadata_lock_);
            process_group_ = value;
        }
        void session(pid_t value) {
            arch::interrupt_spinlock_guard guard(metadata_lock_);
            session_ = value;
        }
        mm::shptr<fs::open_file> controlling_terminal() const {
            arch::interrupt_spinlock_guard guard(metadata_lock_);
            return controlling_terminal_;
        }
        void controlling_terminal(const mm::shptr<fs::open_file>& value) {
            arch::interrupt_spinlock_guard guard(metadata_lock_);
            controlling_terminal_ = value;
        }
        bool claim_controlling_terminal(const mm::shptr<fs::open_file>& value) {
            arch::interrupt_spinlock_guard guard(metadata_lock_);
            if (!value || pid_ != session_ || controlling_terminal_)
                return false;
            controlling_terminal_ = value;
            return true;
        }
        const char* name() const {
            return name_;
        }
        process_state state() const {
            return static_cast<process_state>(__atomic_load_n(&state_, __ATOMIC_ACQUIRE));
        }
        void state(process_state value) {
            __atomic_store_n(&state_, static_cast<u8>(value), __ATOMIC_RELEASE);
        }
        i32 exit_status() const {
            return exit_status_;
        }
        i32 wait_status() const {
            return termination_signal_ != 0 ? wait_status_from_signal(termination_signal_)
                                            : wait_status_from_exit(exit_status_);
        }
        i32 stop_status() const {
            return wait_status_from_stop(stop_signal_);
        }
        bool stop_event_pending() const {
            return __atomic_load_n(&stop_event_, __ATOMIC_ACQUIRE) != 0;
        }
        bool continued_event_pending() const {
            return __atomic_load_n(&continued_event_, __ATOMIC_ACQUIRE) != 0;
        }
        void clear_stop_event() {
            __atomic_store_n(&stop_event_, 0, __ATOMIC_RELEASE);
        }
        void clear_continued_event() {
            __atomic_store_n(&continued_event_, 0, __ATOMIC_RELEASE);
        }
        void signal_state_change(signal_number signal);
        void interrupt_signal_io(signal_number signal);
        void request_kill();
        i64 alarm(u32 seconds, u64 now, u32 hz);
        void expire_alarm(u64 now);
        void signal_termination(signal_number signal) {
            termination_signal_ = static_cast<i32>(signal);
        }
        signal_number termination_signal() const {
            return static_cast<signal_number>(termination_signal_);
        }

        signal_state& signals() {
            return signals_;
        }
        const signal_state& signals() const {
            return signals_;
        }

        /// Called by a thread that is exiting.  When the last live
        /// thread is gone the process becomes a zombie carrying `status`
        /// until its parent reaps it with proc::waitpid().
        bool thread_exited(thread* value, i32 status);
        void request_exit(thread* caller, i32 status);
        bool process_exit_requested() const {
            return __atomic_load_n(&exit_requested_, __ATOMIC_ACQUIRE) != 0;
        }
        i32 requested_exit_status() const {
            return __atomic_load_n(&requested_exit_status_, __ATOMIC_ACQUIRE);
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
        bool block_signal_waiter(thread* value);
        void remove_signal_waiter(thread* value);
        bool signal_waiter_timed_out(thread* value);
        void expire_signal_waiters(u64 now);
        void wake_signal_waiters();

        bool              add_thread(const mm::shptr<thread>& value);
        mm::shptr<thread> thread_at(u32 index) const;
        u32               thread_count() const {
            return thread_count_;
        }

        bool                     install_standard_streams();
        mm::shptr<fs::open_file> descriptor(fd_t fd) const;
        bool set_descriptor(fd_t fd, const mm::shptr<fs::open_file>& value);
        bool set_descriptor(fd_t fd, const mm::shptr<fs::open_file>& value, u32 flags);
        bool clear_descriptor(fd_t fd);
        void close_on_exec_descriptors();
        optional<fd_t> duplicate_descriptor(fd_t old_fd);
        optional<fd_t> duplicate_descriptor(fd_t old_fd, fd_t minimum, u32 flags);
        u32            descriptor_flags(fd_t fd) const;
        bool           set_descriptor_flags(fd_t fd, u32 flags);
        optional<fd_t> duplicate_descriptor_to(fd_t old_fd, fd_t new_fd);

        /// Load an ET_EXEC/ET_DYN image with the shared ELF loader into
        /// this process address space.  This prepares the image only;
        /// user entry/TSS return is an architecture step after this.
        bool load_user_image(span<const u8> bytes, u64* entry_out);
        bool load_user_image(const void* bytes, usize length, u64* entry_out) {
            return load_user_image(span<const u8>(static_cast<const u8*>(bytes), length),
                                   entry_out);
        }
        /// x86_64 thread pointer (%fs base) for this process's static
        /// TLS block; 0 when the image declares no PT_TLS.
        u64 tls_tp() const {
            return tls_tp_;
        }
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
                                        u64 user_stack_base = 0,
                                        usize user_stack_size = 0);
        struct thread_stack_snapshot {
            u64  stack_base = 0;
            usize stack_size = 0;
            bool detached = false;
            bool supported = false;
        };
        mm::shptr<thread> thread_stack_info(tid_t id, thread_stack_snapshot& snapshot);
        /// Find a thread by its process-local tid, retaining it while a
        /// blocking join syscall operates on it.
        mm::shptr<thread>     thread_by_tid(tid_t id);
        const mm::address_space* address_space() const {
            return address_space_.attached() ? &address_space_ : nullptr;
        }

        const mm::address_space& memory_space() const {
            return address_space_;
        }

        mm::address_space* address_space() {
            return address_space_.attached() ? &address_space_ : nullptr;
        }

        /// Mutable process address space for mappings owned by this process.
        mm::address_space& memory_space() {
            return address_space_;
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
        u64 map_file(fd_t fd, vaddr_t addr, bytes_t len, map_protection prot,
                     map_flags flags, offset_t off);

        /// munmap: drop [addr, addr + len) rounded up to pages; parts
        /// of mapped regions outside the range are kept.  Returns 0,
        /// or a negated errno.  Unmapping never-mapped space succeeds.
        i32 unmap(u64 addr, usize len);

        i32 protect(u64 addr, usize len, map_protection prot);

        i32 msync(u64 addr, usize len, u32 flags);

        /// The mmap_region covering `addr`, or nullptr — the entry
        /// point for the future user page-fault path.
        mmap_region* find_region(u64 addr);

        i32 resolve_shared_futex(u64 addr, shared_futex_mapping& out);

        /// Snapshot [addr, addr + len) into `out` as one more entry of an
        /// asynchronous operation's buffer list: 0, or a negated errno.
        /// `destination` marks a buffer the operation will fill (see
        /// mm::pinned_buffer).  v1 resolves each entry to a single backing
        /// region — a region is a contiguous frame run and the direct map is
        /// linear, so an entry is one kernel window a device can be handed —
        /// and answers -EFAULT for a range that crosses out of one.  A process
        /// with no address space should not be here at all: its pointers are
        /// kernel memory, which cannot be unmapped out from under the
        /// operation in the first place (see sys::pin_user_span).
        i32 pin_user(u64 addr, usize len, bool destination, mm::iovec& out);

        /// Copy-on-write fork support (called on the parent): build
        /// `child`'s address space from this one.  Every VMA's backing
        /// gains one reference, private writable pages lose their
        /// write bits on BOTH sides, and the child's PTEs start
        /// read-only — the first write anywhere faults into a private
        /// copy.  tls_tp_ is copied as well.
        bool clone_address_space_cow(process* child);

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
        void snapshot_metadata(metadata_snapshot& out) const {
            arch::interrupt_spinlock_guard guard(metadata_lock_);
            out.process_group        = process_group_;
            out.session              = session_;
            out.uid                  = uid_;
            out.gid                  = gid_;
            out.controlling_terminal = controlling_terminal_;
        }

        bool exec_user_image(span<const u8> bytes, char* const argv[], char* const envp[],
                             u64* entry_out, u64* stack_out);
        bool exec_user_image(const void* bytes, usize length, char* const argv[],
                             char* const envp[], u64* entry_out, u64* stack_out) {
            return exec_user_image(span<const u8>(static_cast<const u8*>(bytes), length),
                                   argv, envp, entry_out, stack_out);
        }
        core::task<exec_result>
        exec_user_file_async(mm::shptr<fs::open_file> file, usize length,
                             char* const argv[], char* const envp[]);
        core::task<bool> load_user_file_async(const mm::shptr<fs::open_file>& file,
                                              usize length, u64* entry_out);

        /// Fork: the child's first thread, resuming `uctx` inside the
        /// parent's ring-3 call site with rax = 0.
        mm::shptr<thread> create_fork_thread(const arch::user_context& uctx,
                                              signal_mask blocked_signals,
                                              u64 user_stack_base = 0,
                                              usize user_stack_size = 0);

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
        bool register_mapping(u64 base, usize pages, map_protection prot,
                              mm::phys_region* region);
        bool build_user_stack(char* const argv[], char* const envp[], u64* rsp_out);
        void discard_image();
        void reset_signal_stacks_for_exec();
        bool clone_tls_for_thread(tid_t id, u64& fs_base, u64& mapping_base,
                                  usize& mapping_pages);
        void replace_image(process& staging);
        /// Reserve an address, install PTEs without mmap_lock_, then publish
        /// the VMA.  On failure the reservation and PTEs are rolled back and
        /// `region` stays the caller's.
        bool   install_mapping(u64 hint, usize pages, map_protection prot,
                               mm::phys_region*                region,
                               const mm::shptr<fs::open_file>& file, u64 file_offset,
                               u64& base_out, usize region_page = 0, bool shared = false,
                               bool stack = false);
        bool grow_stack(mmap_region* vma, u64 addr, bool write);
        pid_t  pid_;
        pid_t  parent_pid_;
        pgrp_t process_group_;
        pid_t  session_;
        mm::shptr<fs::open_file> controlling_terminal_;
        u32                      uid_ = 0, euid_ = 0, gid_ = 0, egid_ = 0;
        u32                      umask_ = 0;
        gid_t                    groups_[max_supplementary_groups]{};
        u32                      group_count_ = 0;
        char                     name_[32]{};
        // These are observed by registry, scheduler, and wait paths that do
        // not hold lifecycle_lock_. Keep the byte-sized representation
        // and publish transitions explicitly rather than widening the record.
        u8           state_              = static_cast<u8>(process_state::fresh);
        i32          exit_status_        = 0;
        i32          termination_signal_ = 0;
        i32          stop_signal_        = 0;
        u8           stop_event_         = 0;
        u8           continued_event_    = 0;
        u8           exit_requested_     = 0;
        i32          requested_exit_status_ = 0;
        signal_state signals_{};
        u64          alarm_deadline_     = 0;

        mm::shptr<thread>        threads_[max_threads_per_process];
        u32                      thread_count_ = 0;
        thread*                  waiters_[max_waiters_per_process]{};
        signal_waiter_registry   signal_waiters_{};
        fd_table                 descriptors_;
        mm::address_space       address_space_;
        u64                      tls_tp_ = 0;
        astia_rlimit             rlimit_nofile_{max_process_fds, max_process_fds};
        astia_rlimit             rlimit_as_{~u64{0}, ~u64{0}};
        u64                      mapped_bytes_      = 0;
        cwd_context              cwd_{};
        mmap_region*             mmap_regions_      = nullptr; // base-sorted
        // Lifecycle/thread state is process-local; keeping this lock on the
        // object avoids serializing unrelated processes.
        core::spinlock lifecycle_lock_{};
        // Credentials, groups, session/job-control metadata, and rlimits are
        // read frequently but are independent of thread lifetime and waits.
        mutable core::spinlock metadata_lock_{};
        core::spinlock         mmap_lock_{};
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
