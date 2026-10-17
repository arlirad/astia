#pragma once

#include "fd_table.hpp"
#include "mmap_region.hpp"
#include "thread.hpp"
#include <astia/arch/page_map.hpp>
#include <astia/common.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/mm/pinned_buffer.hpp>
#include <astia/mm/shptr.hpp>

namespace astia::proc {

    using pid_t = u32;

    inline constexpr u32 max_process_threads = 16;
    inline constexpr u32 max_process_waiters = 4;

    /// Userspace spawn limits for the startup frame (argv/envp).
    inline constexpr u32   max_spawn_args     = 16;
    inline constexpr u32   max_spawn_env      = 16;
    inline constexpr usize spawn_string_bytes = 2048;

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

        pid_t pid() const {
            return pid_;
        }
        pid_t parent_pid() const {
            return parent_pid_;
        }
        const char* name() const {
            return name_;
        }
        process_state state() const {
            return state_;
        }
        void state(process_state value) {
            state_ = value;
        }
        i32 exit_status() const {
            return exit_status_;
        }

        /// Called by a thread that is exiting.  When the last live
        /// thread is gone the process becomes a zombie carrying `status`
        /// until its parent reaps it with proc::waitpid().
        void thread_exited(thread* value, i32 status);

        /// Move the process's owning reference to a dead thread out of
        /// the thread table.  Used to defer destruction: the dying
        /// thread still runs on its own kernel stack.
        mm::shptr<thread> take_thread(thread* value);
        /// Drop the process's owning reference to a thread.  Only safe
        /// when the target can no longer run (joined or deferred).
        void release_thread(thread* value);
        /// Threads blocked in proc::waitpid() for one of our children.
        bool add_waiter(thread* value);
        void remove_waiter(thread* value);
        void wake_waiters();

        bool              add_thread(const mm::shptr<thread>& value);
        mm::shptr<thread> thread_at(u32 index) const;
        u32               thread_count() const {
            return thread_count_;
        }

        bool                     install_standard_streams();
        mm::shptr<fs::open_file> descriptor(i32 fd) const;
        bool set_descriptor(i32 fd, const mm::shptr<fs::open_file>& value);
        bool clear_descriptor(i32 fd);
        i32  duplicate_descriptor(i32 old_fd);
        i32  duplicate_descriptor_to(i32 old_fd, i32 new_fd);

        /// Load an ET_EXEC/ET_DYN image with the shared ELF loader into
        /// this process address space.  This prepares the image only;
        /// user entry/TSS return is an architecture step after this.
        bool load_user_image(const void* bytes, usize length, u64* entry_out);
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
        mm::shptr<thread> create_thread(u64 entry, u64 user_stack_top, u64 argument);
        /// Find a thread by its process-local tid, retaining it while a
        /// blocking join syscall operates on it.
        mm::shptr<thread>     thread_by_tid(tid_t id);
        const arch::page_map* address_space() const {
            return address_space_;
        }

        /// Mutable view for the syscall/ring layer, which maps kernel-owned
        /// pages (the I/O ring) into this process' user half on setup.
        arch::page_map* address_space() {
            return address_space_;
        }

        /// mmap: anonymous private memory; `addr` is a placement hint
        /// (0 = the kernel picks the highest hole below the mmap
        /// ceiling).  Returns the mapped base, or a negated errno.
        u64 map_anonymous(u64 addr, usize len, u32 prot, u32 flags);

        /// mmap of an open file, private (snapshot) semantics: the
        /// file's bytes are captured into fresh frames at map time,
        /// writes land on the private copy, and the mapping keeps the
        /// open file description alive across close().  `fd` resolves
        /// in this process' table; `off` must be page-aligned.
        u64 map_file(i32 fd, u64 addr, usize len, u32 prot, u32 flags, u64 off);

        /// munmap: drop [addr, addr + len) rounded up to pages; parts
        /// of mapped regions outside the range are kept.  Returns 0,
        /// or a negated errno.  Unmapping never-mapped space succeeds.
        i32 unmap(u64 addr, usize len);

        /// The mmap_region covering `addr`, or nullptr — the entry
        /// point for the future user page-fault path.
        mmap_region* find_region(u64 addr);

        /// Snapshot [addr, addr + len) into `out` for the duration of an
        /// asynchronous operation: 0, or a negated errno.  `destination`
        /// marks a buffer the operation will fill (see mm::pinned_buffer).
        /// v1 resolves the range to a single backing region — a region is
        /// a contiguous frame run and the direct map is linear, so the
        /// whole pin is one kernel window a device can be handed — and
        /// answers -EFAULT for a range that crosses out of one.  A process
        /// with no address space reports 0 and pins nothing: its pointers
        /// are kernel memory, which cannot be unmapped out from under the
        /// operation in the first place.
        i32 pin_user(u64 addr, usize len, bool destination, mm::pinned_buffer& out);

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

        /// Fork: the child's first thread, resuming `uctx` inside the
        /// parent's ring-3 call site with rax = 0.
        mm::shptr<thread> create_fork_thread(const arch::user_context& uctx);

        /// Resolve a ring-3 write fault: a uniquely-held CoW page
        /// regains its write bit, otherwise the page is copied out and
        /// the VMA splits around it.  True when the faulting
        /// instruction will now succeed.
        bool cow_fault(u64 addr, bool write);

        private:
        /// File the VMA entry for an already-mapped range [base, base
        /// + pages): `region` adopts the `pages` contiguous frames the
        /// caller mapped PTEs for, and the entry takes over its
        /// reference.
        bool register_mapping(u64 base, usize pages, u32 prot, mm::phys_region* region);
        bool clone_tls_for_thread(tid_t id, u64& fs_base, u64& mapping_base,
                                  usize& mapping_pages);
        /// Map PTEs for [base, base + pages) and insert the VMA entry,
        /// which takes over `region`'s reference (`file`/`file_offset`
        /// recorded on it, both empty for anonymous ranges).  On
        /// failure the PTEs are rolled back and `region` stays the
        /// caller's.
        bool  install_mapping(u64 base, usize pages, u32 prot, mm::phys_region* region,
                              const mm::shptr<fs::open_file>& file, u64 file_offset,
                              usize region_page = 0, bool shared = false);
        pid_t pid_;
        pid_t parent_pid_;
        char  name_[32]{};
        process_state state_       = process_state::fresh;
        i32           exit_status_ = 0;

        mm::shptr<thread> threads_[max_process_threads];
        u32               thread_count_ = 0;
        thread*           waiters_[max_process_waiters]{};
        fd_table          descriptors_;
        arch::page_map*   address_space_ = nullptr;
        u64               tls_tp_        = 0;
        mmap_region*      mmap_regions_  = nullptr; // base-sorted
        core::spinlock    mmap_lock_{};
    };

    /// Queue a just-died thread for destruction once its kernel stack
    /// is no longer executing anywhere.  Reaped by reap_dead_threads()
    /// after the next scheduler context switch.
    void defer_dead_thread(const mm::shptr<thread>& value);
    /// Destroy queued dead threads.  Called from the scheduler only
    /// after switching away from a context, where no queued thread can
    /// still be running on its own stack.
    void reap_dead_threads();

} // namespace astia::proc
