#pragma once

#include <astia/arch/page_map.hpp>
#include <astia/common.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/mm/shptr.hpp>
#include <astia/proc/fd_table.hpp>
#include <astia/proc/mmap_region.hpp>
#include <astia/proc/thread.hpp>

namespace astia::proc {

    using pid_t = u32;

    inline constexpr u32 max_process_threads = 16;
    inline constexpr u32 max_process_waiters = 4;

    /// Userspace spawn limits for the startup frame (argv/envp).
    inline constexpr u32   max_spawn_args      = 16;
    inline constexpr u32   max_spawn_env       = 16;
    inline constexpr usize spawn_string_bytes  = 2048;

    enum class process_state : u8 {
        fresh,
        runnable,
        stopping,
        zombie,
        dead,
    };

    /// Process-owned lifetime and descriptor boundary.  The process
    /// owns thread references and open-file descriptions; threads keep
    /// a non-owning backlink to their process.
    class process {
        public:
        process(pid_t id, pid_t parent, const char* name);
        ~process();

        auto pid() const -> pid_t {
            return pid_;
        }
        auto parent_pid() const -> pid_t {
            return parent_pid_;
        }
        auto name() const -> const char* {
            return name_;
        }
        auto state() const -> process_state {
            return state_;
        }
        auto state(process_state value) -> void {
            state_ = value;
        }
        auto exit_status() const -> i32 {
            return exit_status_;
        }

        /// Called by a thread that is exiting.  When the last live
        /// thread is gone the process becomes a zombie carrying `status`
        /// until its parent reaps it with proc::waitpid().
        auto thread_exited(thread* value, i32 status) -> void;

        /// Move the process's owning reference to a dead thread out of
        /// the thread table.  Used to defer destruction: the dying
        /// thread still runs on its own kernel stack.
        auto take_thread(thread* value) -> mm::shptr<thread>;
        /// Drop the process's owning reference to a thread.  Only safe
        /// when the target can no longer run (joined or deferred).
        auto release_thread(thread* value) -> void;
        /// Threads blocked in proc::waitpid() for one of our children.
        auto add_waiter(thread* value) -> bool;
        auto remove_waiter(thread* value) -> void;
        auto wake_waiters() -> void;

        auto add_thread(const mm::shptr<thread>& value) -> bool;
        auto thread_at(u32 index) const -> mm::shptr<thread>;
        auto thread_count() const -> u32 {
            return thread_count_;
        }

        auto install_standard_streams() -> bool;
        auto descriptor(i32 fd) const -> mm::shptr<fs::open_file>;
        auto set_descriptor(i32 fd, const mm::shptr<fs::open_file>& value) -> bool;
        auto clear_descriptor(i32 fd) -> bool;
        auto duplicate_descriptor(i32 old_fd) -> i32;
        auto duplicate_descriptor_to(i32 old_fd, i32 new_fd) -> i32;

        /// Load an ET_EXEC/ET_DYN image with the shared ELF loader into
        /// this process address space.  This prepares the image only;
        /// user entry/TSS return is an architecture step after this.
        auto load_user_image(const void* bytes, usize length, u64* entry_out) -> bool;
        /// x86_64 thread pointer (%fs base) for this process's static
        /// TLS block; 0 when the image declares no PT_TLS.
        auto tls_tp() const -> u64 {
            return tls_tp_;
        }
        /// Create the process's first user thread.  Builds the
        /// userspace startup frame (argc/argv/envp, arch-neutral
        /// content) on the freshly mapped user stack and enters at
        /// `entry` with the computed stack pointer.  argv/envp may be
        /// null; an empty argv defaults to argv[0] = process name.
        auto create_user_thread(u64 entry, char* const argv[] = nullptr,
                                char* const envp[] = nullptr) -> mm::shptr<thread>;
        /// Create an additional user thread in this address space.  The
        /// supplied stack belongs to userspace (pthread_create maps it);
        /// `argument` becomes the new entry's first ABI argument.
        auto create_thread(u64 entry, u64 user_stack_top, u64 argument)
            -> mm::shptr<thread>;
        /// Find a thread by its process-local tid, retaining it while a
        /// blocking join syscall operates on it.
        auto thread_by_tid(tid_t id) -> mm::shptr<thread>;
        auto address_space() const -> const arch::page_map* {
            return address_space_;
        }

        /// Mutable view for the syscall/ring layer, which maps kernel-owned
        /// pages (the I/O ring) into this process' user half on setup.
        auto address_space() -> arch::page_map* {
            return address_space_;
        }

        /// mmap: anonymous private memory; `addr` is a placement hint
        /// (0 = the kernel picks the highest hole below the mmap
        /// ceiling).  Returns the mapped base, or a negated errno.
        auto map_anonymous(u64 addr, usize len, u32 prot, u32 flags) -> u64;

        /// mmap of an open file, private (snapshot) semantics: the
        /// file's bytes are captured into fresh frames at map time,
        /// writes land on the private copy, and the mapping keeps the
        /// open file description alive across close().  `fd` resolves
        /// in this process' table; `off` must be page-aligned.
        auto map_file(i32 fd, u64 addr, usize len, u32 prot, u32 flags,
                      u64 off) -> u64;

        /// munmap: drop [addr, addr + len) rounded up to pages; parts
        /// of mapped regions outside the range are kept.  Returns 0,
        /// or a negated errno.  Unmapping never-mapped space succeeds.
        auto unmap(u64 addr, usize len) -> i32;

        /// The mmap_region covering `addr`, or nullptr — the entry
        /// point for the future user page-fault path.
        auto find_region(u64 addr) -> mmap_region*;

        /// Copy-on-write fork support (called on the parent): build
        /// `child`'s address space from this one.  Every VMA's backing
        /// gains one reference, private writable pages lose their
        /// write bits on BOTH sides, and the child's PTEs start
        /// read-only — the first write anywhere faults into a private
        /// copy.  tls_tp_ is copied as well.
        auto clone_address_space_cow(process* child) -> bool;

        /// Share this process's open file descriptions with a fork
        /// child: the child's entries reference the SAME descriptions
        /// (POSIX — file offsets move together).
        auto copy_descriptors_from(const process& other) -> bool;

        /// Fork: the child's first thread, resuming `uctx` inside the
        /// parent's ring-3 call site with rax = 0.
        auto create_fork_thread(const arch::user_context& uctx) -> mm::shptr<thread>;

        /// Resolve a ring-3 write fault: a uniquely-held CoW page
        /// regains its write bit, otherwise the page is copied out and
        /// the VMA splits around it.  True when the faulting
        /// instruction will now succeed.
        auto cow_fault(u64 addr, bool write) -> bool;

        private:
        /// File the VMA entry for an already-mapped range [base, base
        /// + pages): `region` adopts the `pages` contiguous frames the
        /// caller mapped PTEs for, and the entry takes over its
        /// reference.
        auto register_mapping(u64 base, usize pages, u32 prot,
                              mm::phys_region* region) -> bool;
        auto clone_tls_for_thread(tid_t id, u64& fs_base, u64& mapping_base,
                                  usize& mapping_pages) -> bool;
        /// Map PTEs for [base, base + pages) and insert the VMA entry,
        /// which takes over `region`'s reference (`file`/`file_offset`
        /// recorded on it, both empty for anonymous ranges).  On
        /// failure the PTEs are rolled back and `region` stays the
        /// caller's.
        auto install_mapping(u64 base, usize pages, u32 prot,
                             mm::phys_region* region,
                             const mm::shptr<fs::open_file>& file,
                             u64 file_offset, usize region_page = 0,
                             bool shared = false) -> bool;
        pid_t         pid_;
        pid_t         parent_pid_;
        char          name_[32]{};
        process_state state_      = process_state::fresh;
        i32           exit_status_ = 0;

        mm::shptr<thread> threads_[max_process_threads];
        u32               thread_count_ = 0;
        thread*           waiters_[max_process_waiters]{};
        fd_table          descriptors_;
        arch::page_map*   address_space_ = nullptr;
        u64               tls_tp_        = 0;
        mmap_region*      mmap_regions_  = nullptr; // base-sorted
        core::spinlock    mmap_lock_ {};
    };

    /// Queue a just-died thread for destruction once its kernel stack
    /// is no longer executing anywhere.  Reaped by reap_dead_threads()
    /// after the next scheduler context switch.
    auto defer_dead_thread(const mm::shptr<thread>& value) -> void;
    /// Destroy queued dead threads.  Called from the scheduler only
    /// after switching away from a context, where no queued thread can
    /// still be running on its own stack.
    auto reap_dead_threads() -> void;

} // namespace astia::proc
