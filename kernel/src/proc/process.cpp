#include "process.hpp"

#include "../sys/aio.hpp"
#include "proc.hpp"
#include "scheduler.hpp"
#include <astia/api/syscalls.h>
#include <astia/api/termios.h>
#include <astia/arch/cpu.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/share.hpp>

void* operator new[](astia::usize size, void* where) noexcept;

namespace astia::proc {

    i64 process::change_directory(const char* path) {
        if (path == nullptr || path[0] == '\0')
            return -EINVAL;
        char full[fs::path_max]{};
        if (path[0] == '/') {
            usize i = 0;
            for (; path[i] != '\0' && i + 1 < fs::path_max; ++i)
                full[i] = path[i];
            if (path[i] != '\0')
                return -ENAMETOOLONG;
            full[i] = '\0';
        }
        else {
            usize at = 0;
            for (; cwd_[at] != '\0' && at + 1 < fs::path_max; ++at)
                full[at] = cwd_[at];
            if (at > 1)
                full[at++] = '/';
            usize source = 0;
            for (; path[source] != '\0' && at + 1 < fs::path_max; ++source, ++at)
                full[at] = path[source];
            if (path[source] != '\0')
                return -ENAMETOOLONG;
            full[at] = '\0';
        }
        if (full[0] == '\0')
            return -EINVAL;
        if (full[0] != '/')
            return -EINVAL;
        char       resolved[fs::path_max]{};
        const long resolve_rc = fs::resolve_path(full, resolved, sizeof resolved, true);
        if (resolve_rc < 0)
            return resolve_rc;
        if (fs::node_type(resolved) == fs::directory_unknown)
            return -ENOENT;
        if (fs::node_type(resolved) != fs::directory_dir)
            return -ENOTDIR;
        usize i = 0;
        for (; resolved[i] != '\0'; ++i)
            cwd_[i] = resolved[i];
        cwd_[i]                = '\0';
        cwd_[fs::path_max - 1] = '\0';
        return 0;
    }

    i64 process::copy_cwd(char* buffer, usize length) const {
        if (buffer == nullptr || length == 0)
            return -EINVAL;
        usize i = 0;
        for (; cwd_[i] != '\0' && i + 1 < length; ++i)
            buffer[i] = cwd_[i];
        if (cwd_[i] != '\0')
            return -ERANGE;
        buffer[i] = '\0';
        return static_cast<i64>(i + 1);
    }

    i64 process::compose_path(const char* path, char* buffer, usize length) const {
        if (path == nullptr || buffer == nullptr || length == 0)
            return -EINVAL;
        if (path[0] == '/') {
            usize i = 0;
            for (; path[i] != '\0' && i + 1 < length; ++i)
                buffer[i] = path[i];
            if (path[i] != '\0')
                return -ENAMETOOLONG;
            buffer[i] = '\0';
            return 0;
        }
        if (path[0] == '.' && path[1] == '\0') {
            return copy_cwd(buffer, length) < 0 ? -ERANGE : 0;
        }
        usize at = 0;
        for (; cwd_[at] != '\0' && at + 1 < length; ++at)
            buffer[at] = cwd_[at];
        if (at > 1 && at + 1 < length)
            buffer[at++] = '/';
        usize source = 0;
        for (; path[source] != '\0' && at + 1 < length; ++source, ++at)
            buffer[at] = path[source];
        if (path[source] != '\0')
            return -ENAMETOOLONG;
        buffer[at] = '\0';
        return 0;
    }

    i64 process::compose_path_at(fd_t dirfd, const char* path, char* buffer,
                                 usize length) const {
        if (path == nullptr || buffer == nullptr || length == 0)
            return -EINVAL;
        if (path[0] == '/')
            return compose_path(path, buffer, length);
        if (dirfd.value == -100)
            return compose_path(path, buffer, length);
        auto directory = descriptor(dirfd);
        if (!directory)
            return -EBADF;
        if (!directory->path() || !directory->mode().is_directory())
            return -ENOTDIR;
        usize       at   = 0;
        const char* base = directory->path();
        for (; base[at] != '\0' && at + 1 < length; ++at)
            buffer[at] = base[at];
        if (base[at] != '\0')
            return -ENAMETOOLONG;
        if (at > 1 && at + 1 < length)
            buffer[at++] = '/';
        usize i = 0;
        for (; path[i] != '\0' && at + 1 < length; ++i, ++at)
            buffer[at] = path[i];
        if (path[i] != '\0')
            return -ENAMETOOLONG;
        buffer[at] = '\0';
        return 0;
    }

    namespace {
        core::spinlock process_lock;

        inline constexpr u32 dead_capacity = 16;
        // Placement-constructed like the process registry: keeps the
        // global trivially destructible in freestanding code.
        alignas(mm::shptr<thread>) u8
            dead_storage[sizeof(mm::shptr<thread>) * dead_capacity];
        mm::shptr<thread>* dead_queue = nullptr;
        u32                dead_count = 0;
    }

    void defer_dead_thread(const mm::shptr<thread>& value) {
        if (!value) {
            return;
        }
        core::spinlock_guard guard(process_lock);
        if (dead_queue == nullptr) {
            dead_queue = ::new (dead_storage) mm::shptr<thread>[dead_capacity];
        }
        if (dead_count == dead_capacity) {
            return; // queue full: the thread-table ref keeps it alive
        }
        dead_queue[dead_count++] = value;
    }

    // Reap queued dead threads.  Called from the scheduler only
    // after switching away from a context, where no queued thread
    // can still be running on its own stack.
    void reap_dead_threads() {
        if (dead_queue == nullptr) {
            return;
        }
        mm::shptr<thread> pending[dead_capacity];
        u32               count = 0;
        {
            core::spinlock_guard guard(process_lock);
            for (u32 i = 0; i < dead_count; ++i) {
                pending[i]    = dead_queue[i];
                dead_queue[i] = {};
            }
            count      = dead_count;
            dead_count = 0;
        }
        // Dropping the last reference runs ~thread, which frees the
        // dead thread's kernel stack.  Safe here: the scheduler has
        // already switched away from any context these ran on.
        for (u32 i = 0; i < count; ++i) {
            pending[i] = {};
        }
    }

    namespace {
        usize cstr_length(const char* value, usize limit) {
            usize length = 0;
            while (value != nullptr && value[length] != '\0' && length < limit) {
                ++length;
            }
            return length;
        }

        // Arch-neutral write into a user region that is mapped in the
        // child but not active here.  The caller owns the physical
        // frames backing the region, so they are reachable through the
        // kernel's direct-map translation (mm::frame_allocator).
        bool write_user(u64 base, const u64* frames, u32 frame_count, u64 vaddr,
                        const void* src, usize length) {
            const auto* bytes = static_cast<const u8*>(src);
            while (length != 0) {
                if (vaddr < base) {
                    return false;
                }
                const u64 offset = vaddr - base;
                if (offset / mm::frame_allocator::frame_size >= frame_count) {
                    return false;
                }
                auto* page = static_cast<u8*>(mm::kernel_frames().to_virt(
                    frames[offset / mm::frame_allocator::frame_size]));
                if (page == nullptr) {
                    return false;
                }
                const usize within = offset % mm::frame_allocator::frame_size;
                usize       chunk  = mm::frame_allocator::frame_size - within;
                if (chunk > length) {
                    chunk = length;
                }
                for (usize i = 0; i < chunk; ++i) {
                    page[within + i] = bytes[i];
                }
                bytes += chunk;
                vaddr += chunk;
                length -= chunk;
            }
            return true;
        }

        bool write_user_u64(u64 base, const u64* frames, u32 frame_count, u64 vaddr,
                            u64 value) {
            return write_user(base, frames, frame_count, vaddr, &value, sizeof(value));
        }

        // Build the userspace startup frame on the child's user stack.
        // Content is the SysV shape shared by astia's userspace ABI:
        //
        //   rsp -> argc, argv[0..argc-1], NULL,
        //          envp[0..envc-1], NULL, auxv AT_NULL
        //          ... 16-byte aligned ...
        //          NUL-terminated argv/envp strings, packed from the
        //          top of the stack downwards
        //
        // Pure data layout in plain C++; the architecture only consumes
        // the resulting stack pointer via context_prepare_user.
        bool build_initial_stack(u64 base, u64 top, const u64* frames, u32 frame_count,
                                 const char* const* argv, u32 argc,
                                 const char* const* envp, u32 envc, u64* rsp_out) {
            u64 cursor = top;
            u64 argv_address[max_spawn_args]{};
            u64 envp_address[max_spawn_env]{};

            // Strings first, packed from the top down.
            for (u32 i = envc; i > 0; --i) {
                const char* text   = envp[i - 1];
                const usize length = cstr_length(text, spawn_string_bytes);
                if (cursor < base + length + 1) {
                    return false;
                }
                cursor -= length + 1;
                if (!write_user(base, frames, frame_count, cursor, text, length) ||
                    !write_user(base, frames, frame_count, cursor + length, "", 1)) {
                    return false;
                }
                envp_address[i - 1] = cursor;
            }
            for (u32 i = argc; i > 0; --i) {
                const char* text   = argv[i - 1];
                const usize length = cstr_length(text, spawn_string_bytes);
                if (cursor < base + length + 1) {
                    return false;
                }
                cursor -= length + 1;
                if (!write_user(base, frames, frame_count, cursor, text, length) ||
                    !write_user(base, frames, frame_count, cursor + length, "", 1)) {
                    return false;
                }
                argv_address[i - 1] = cursor;
            }

            // The startup block: argc + argv[] + NULL + envp[] + NULL
            // + auxv AT_NULL pair, 16-byte aligned at entry.
            const u64 block = sizeof(u64) * (1 + argc + 1 + envc + 1 + 2);
            if (cursor < base + block) {
                return false;
            }
            cursor = (cursor - block) & ~u64{15};
            if (cursor < base) {
                return false;
            }

            u64 at = cursor;
            if (!write_user_u64(base, frames, frame_count, at, argc)) {
                return false;
            }
            at += sizeof(u64);
            for (u32 i = 0; i < argc; ++i, at += sizeof(u64)) {
                if (!write_user_u64(base, frames, frame_count, at, argv_address[i])) {
                    return false;
                }
            }
            if (!write_user_u64(base, frames, frame_count, at, 0)) { // argv NULL
                return false;
            }
            at += sizeof(u64);
            for (u32 i = 0; i < envc; ++i, at += sizeof(u64)) {
                if (!write_user_u64(base, frames, frame_count, at, envp_address[i])) {
                    return false;
                }
            }
            if (!write_user_u64(base, frames, frame_count, at, 0)) { // envp NULL
                return false;
            }
            at += sizeof(u64);
            // auxv AT_NULL terminator (type = 0, value = 0).
            if (!write_user_u64(base, frames, frame_count, at, 0) ||
                !write_user_u64(base, frames, frame_count, at + sizeof(u64), 0)) {
                return false;
            }

            *rsp_out = cursor;
            return true;
        }
    } // namespace

    process::process(pid_t id, pid_t parent, const char* name)
        : pid_(id), parent_pid_(parent), process_group_(pgrp_t{static_cast<u32>(id)}),
          session_(id) {
        u32 i = 0;
        while (name != nullptr && name[i] != '\0' && i + 1 < sizeof(name_)) {
            name_[i] = name[i];
            ++i;
        }
        name_[i] = '\0';
    }

    i64 process::setuid(u32 v) {
        if (euid_ != 0 && v != uid_ && v != euid_)
            return -EPERM;
        uid_ = euid_ = v;
        return 0;
    }
    i64 process::seteuid(u32 v) {
        if (euid_ != 0 && v != uid_ && v != euid_)
            return -EPERM;
        euid_ = v;
        return 0;
    }
    i64 process::setgid(u32 v) {
        if (egid_ != 0 && v != gid_ && v != egid_)
            return -EPERM;
        gid_ = egid_ = v;
        return 0;
    }
    i64 process::setegid(u32 v) {
        if (egid_ != 0 && v != gid_ && v != egid_)
            return -EPERM;
        egid_ = v;
        return 0;
    }
    i64 process::setreuid(u32 r, u32 e) {
        if (euid_ != 0 && r != uid_ && r != euid_ && e != uid_ && e != euid_)
            return -EPERM;
        if (r != static_cast<u32>(-1))
            uid_ = r;
        if (e != static_cast<u32>(-1))
            euid_ = e;
        return 0;
    }
    i64 process::setregid(u32 r, u32 e) {
        if (egid_ != 0 && r != gid_ && r != egid_ && e != gid_ && e != egid_)
            return -EPERM;
        if (r != static_cast<u32>(-1))
            gid_ = r;
        if (e != static_cast<u32>(-1))
            egid_ = e;
        return 0;
    }
    bool process::in_group(gid_t group) const {
        if (egid_ == static_cast<u32>(group))
            return true;
        for (u32 i = 0; i < group_count_; ++i)
            if (groups_[i] == group)
                return true;
        return false;
    }
    i64 process::getgroups(i32 count, u32* out) const {
        if (count < 0 || (count != 0 && out == nullptr))
            return -EINVAL;
        if (count == 0)
            return group_count_;
        if (static_cast<u32>(count) < group_count_)
            return -EINVAL;
        for (u32 i = 0; i < group_count_; ++i)
            out[i] = static_cast<u32>(groups_[i]);
        return group_count_;
    }
    i64 process::setgroups(i32 count, const u32* values) {
        if (euid_ != 0)
            return -EPERM;
        if (count < 0 || static_cast<u32>(count) > max_groups ||
            (count != 0 && values == nullptr))
            return -EINVAL;
        group_count_ = static_cast<u32>(count);
        for (u32 i = 0; i < group_count_; ++i)
            groups_[i] = gid_t{values[i]};
        return 0;
    }

    process::~process() {
        // VMA teardown releases frame references under spinlocks. Do not let
        // the timer switch this CPU while the dying process owns one: the
        // next thread could attempt the same lock and deadlock permanently.
        arch::interrupt_guard gate;
        discard_image();
        // Member destructors run after this body, which would otherwise
        // restore IF before their shptr releases. Empty them while the gate
        // is still closed.
        descriptors_.clear_all();
        for (u32 i = 0; i < max_process_threads; ++i) {
            threads_[i] = {};
        }
    }

    void process::discard_image() {
        while (mmap_regions_ != nullptr) {
            mmap_region* victim = mmap_regions_;
            mmap_regions_       = victim->next;
            victim->backing->unmap_range(victim->region_page, victim->pages);
            if (victim->holds_ref)
                victim->backing->release_ref();
            delete victim;
        }
        delete address_space_;
        address_space_ = nullptr;
        tls_tp_        = 0;
        mapped_bytes_  = 0;
    }

    bool process::add_thread(const mm::shptr<thread>& value) {
        if (!value) {
            return false;
        }
        core::spinlock_guard guard(process_lock);
        for (u32 i = 0; i < max_process_threads; ++i) {
            if (!threads_[i]) {
                threads_[i] = value;
                ++thread_count_;
                return true;
            }
        }
        return false;
    }

    mm::shptr<thread> process::thread_at(u32 index) const {
        return index < max_process_threads ? threads_[index] : mm::shptr<thread>{};
    }

    void process::thread_exited(thread* value, i32 status) {
        {
            core::spinlock_guard guard(process_lock);
            if (state_ == process_state::zombie || state_ == process_state::dead) {
                return;
            }
            bool any_alive = false;
            for (u32 i = 0; i < max_process_threads; ++i) {
                if (threads_[i] && threads_[i].get() != value &&
                    threads_[i]->state() != thread_state::dead) {
                    any_alive = true;
                    break;
                }
            }
            if (any_alive) {
                return;
            }
            exit_status_ = status;
            state_       = process_state::zombie;
        }
        // Closing a session leader's terminal hangs up its foreground job.
        // Do this before dropping descriptors so the terminal's foreground
        // process group is still available to the signal fan-out.
        if (controlling_terminal_ != nullptr && session_ == pid_) {
            (void) signal_process_group(pgrp_t{static_cast<u32>(process_group_)},
                                        signal_number::hup);
            controlling_terminal_ = nullptr;
        }
        // Release a child's private ring at exit rather than waiting for
        // waitpid: the parent and subsequent children may still need slots.
        sys::io_exit_process(this);
        // A dead process no longer owns any descriptors.  Release them after
        // its ring is quiesced: a pipe writer left in a zombie would otherwise
        // keep readers blocked until the parent reaps it.
        descriptors_.clear_all();
        // A dynamically-created thread owns a private static-TLS VMA.
        // It is safe to remove after the thread has entered the kernel
        // through pthread_exit: no user instruction will touch %fs
        // again.  The initial process TLS remains process-owned and is
        // reclaimed with the address space.
        if (value != nullptr && value->tls_mapping_base() != 0 &&
            value->tls_mapping_pages() != 0) {
            unmap(value->tls_mapping_base(),
                  value->tls_mapping_pages() * mm::phys_region::frame_size);
        }
        // Waiters block on the *parent* (registered by proc::waitpid);
        // waking must happen on that object, not on the dying process.
        process* parent = find_process(parent_pid_);
        if (parent != nullptr) {
            parent->signals().send(signal_number::chld);
            if (parent->address_space() != nullptr)
                parent->wake_signal_waiters();
            parent->wake_waiters();
        }
    }

    void process::signal_state_change(signal_number signal) {
        if (signal == signal_number::cont || signal == signal_number::kill) {
            thread* wake_list[max_process_threads];
            u32     count = 0;
            {
                core::spinlock_guard guard(process_lock);
                if (state_ != process_state::stopped)
                    return;
                state_ = process_state::runnable;
                if (signal == signal_number::cont)
                    continued_event_ = true;
                for (u32 i = 0; i < max_process_threads; ++i) {
                    thread* value = threads_[i].get();
                    if (value != nullptr && value->state() == thread_state::blocked)
                        wake_list[count++] = value;
                }
            }
            for (u32 i = 0; i < count; ++i)
                scheduler::wake(wake_list[i]);
            process* parent = find_process(parent_pid_);
            if (parent != nullptr) {
                (void) parent->signals().send(signal_number::chld);
                parent->wake_signal_waiters();
                parent->wake_waiters();
            }
            return;
        }
        if (signal != signal_number::stop && signal != signal_number::tstp &&
            signal != signal_number::ttin && signal != signal_number::ttou)
            return;
        {
            core::spinlock_guard guard(process_lock);
            if (state_ == process_state::zombie || state_ == process_state::dead ||
                state_ == process_state::stopped)
                return;
            stop_signal_ = static_cast<i32>(signal);
            stop_event_  = true;
            state_       = process_state::stopped;
            for (u32 i = 0; i < max_process_threads; ++i) {
                thread* value = threads_[i].get();
                if (value != nullptr && value->state() != thread_state::dead)
                    value->state(thread_state::blocked);
            }
        }
        process* parent = find_process(parent_pid_);
        if (parent != nullptr) {
            (void) parent->signals().send(signal_number::chld);
            parent->wake_signal_waiters();
            parent->wake_waiters();
        }
    }

    mm::shptr<thread> process::take_thread(thread* value) {
        core::spinlock_guard guard(process_lock);
        for (u32 i = 0; i < max_process_threads; ++i) {
            if (threads_[i] && threads_[i].get() == value) {
                mm::shptr<thread> taken = threads_[i];
                threads_[i]             = {};
                if (thread_count_ != 0) {
                    --thread_count_;
                }
                return taken;
            }
        }
        return {};
    }

    void process::release_thread(thread* value) {
        mm::shptr<thread> released;
        {
            core::spinlock_guard guard(process_lock);
            for (u32 i = 0; i < max_process_threads; ++i) {
                if (threads_[i] && threads_[i].get() == value) {
                    // Do not run ~thread while process_lock is held.
                    released = static_cast<mm::shptr<thread>&&>(threads_[i]);
                    if (thread_count_ != 0) {
                        --thread_count_;
                    }
                    break;
                }
            }
        }
    }

    bool process::add_waiter(thread* value) {
        if (value == nullptr) {
            return false;
        }
        core::spinlock_guard guard(process_lock);
        for (u32 i = 0; i < max_process_waiters; ++i) {
            if (waiters_[i] == nullptr) {
                waiters_[i] = value;
                return true;
            }
        }
        return false;
    }

    void process::remove_waiter(thread* value) {
        core::spinlock_guard guard(process_lock);
        for (u32 i = 0; i < max_process_waiters; ++i) {
            if (waiters_[i] == value) {
                waiters_[i] = nullptr;
            }
        }
    }

    void process::wake_waiters() {
        thread* wake_list[max_process_waiters];
        u32     count = 0;
        {
            core::spinlock_guard guard(process_lock);
            for (u32 i = 0; i < max_process_waiters; ++i) {
                if (waiters_[i] != nullptr) {
                    wake_list[count++] = waiters_[i];
                    waiters_[i]        = nullptr;
                }
            }
        }
        for (u32 i = 0; i < count; ++i) {
            scheduler::wake(wake_list[i]);
        }
    }

    bool process::add_signal_waiter(thread* value) {
        if (value == nullptr) {
            return false;
        }
        core::spinlock_guard guard(process_lock);
        for (u32 i = 0; i < max_process_signal_waiters; ++i) {
            if (signal_waiters_[i] == nullptr) {
                signal_waiters_[i] = value;
                return true;
            }
        }
        return false;
    }

    void process::remove_signal_waiter(thread* value) {
        core::spinlock_guard guard(process_lock);
        for (u32 i = 0; i < max_process_signal_waiters; ++i) {
            if (signal_waiters_[i] == value) {
                signal_waiters_[i] = nullptr;
            }
        }
    }

    void process::wake_signal_waiters() {
        thread* wake_list[max_process_signal_waiters];
        u32     count = 0;
        {
            core::spinlock_guard guard(process_lock);
            for (u32 i = 0; i < max_process_signal_waiters; ++i) {
                if (signal_waiters_[i] != nullptr) {
                    wake_list[count++] = signal_waiters_[i];
                    signal_waiters_[i] = nullptr;
                }
            }
        }
        for (u32 i = 0; i < count; ++i) {
            scheduler::wake(wake_list[i]);
        }
    }

    bool process::install_standard_streams() {
        const bool installed = descriptors_.install_standard_streams();
        if (installed) {
            auto tty = descriptor(fd_t{STDIN});
            if (tty) {
                controlling_terminal(tty->backing());
                i32 group = static_cast<i32>(static_cast<u32>(process_group_));
                (void) tty->ioctl(TIOCSPGRP, &group);
            }
        }
        return installed;
    }

    mm::shptr<fs::open_file> process::descriptor(fd_t fd) const {
        return descriptors_.get(fd);
    }

    bool process::set_descriptor(fd_t fd, const mm::shptr<fs::open_file>& value) {
        if (fd.value < 0 || static_cast<u64>(fd.value) >= rlimit_nofile_.current)
            return false;
        return descriptors_.set(fd, value);
    }

    bool process::set_descriptor(fd_t fd, const mm::shptr<fs::open_file>& value,
                                 u32 flags) {
        if (fd.value < 0 || static_cast<u64>(fd.value) >= rlimit_nofile_.current)
            return false;
        return descriptors_.set(fd, value, flags);
    }

    void process::close_on_exec_descriptors() {
        descriptors_.close_on_exec();
    }

    bool process::clear_descriptor(fd_t fd) {
        return descriptors_.clear(fd);
    }

    optional<fd_t> process::duplicate_descriptor(fd_t old_fd) {
        return descriptors_.duplicate(old_fd, fd_t{0}, 0,
                                      static_cast<u32>(rlimit_nofile_.current));
    }

    optional<fd_t> process::duplicate_descriptor(fd_t old_fd, fd_t minimum, u32 flags) {
        const u32 limit = static_cast<u32>(rlimit_nofile_.current);
        return descriptors_.duplicate(old_fd, minimum, flags, limit);
    }

    u32 process::descriptor_flags(fd_t fd) const {
        return descriptors_.flags(fd);
    }

    bool process::set_descriptor_flags(fd_t fd, u32 flags) {
        auto object = descriptors_.get(fd);
        return object && descriptors_.set(fd, object, flags);
    }

    optional<fd_t> process::duplicate_descriptor_to(fd_t old_fd, fd_t new_fd) {
        if (new_fd.value < 0 || static_cast<u64>(new_fd.value) >= rlimit_nofile_.current)
            return error_t::emfile;
        return descriptors_.duplicate_to(old_fd, new_fd);
    }

    i64 process::get_rlimit(i32 resource, astia_rlimit* out) const {
        if (out == nullptr)
            return -EFAULT;
        if (resource == RLIMIT_NOFILE)
            *out = rlimit_nofile_;
        else if (resource == RLIMIT_AS)
            *out = rlimit_as_;
        else
            return -EINVAL;
        return 0;
    }

    i64 process::set_rlimit(i32 resource, const astia_rlimit& value) {
        if (value.current > value.maximum)
            return -EINVAL;
        if (resource == RLIMIT_NOFILE) {
            if (value.maximum > max_process_fds)
                return -EINVAL;
            if (value.maximum > rlimit_nofile_.maximum)
                return -EPERM;
            rlimit_nofile_ = value;
            return 0;
        }
        if (resource == RLIMIT_AS) {
            if (value.maximum > rlimit_as_.maximum || value.current < mapped_bytes_)
                return value.maximum > rlimit_as_.maximum ? -EPERM : -EINVAL;
            rlimit_as_ = value;
            return 0;
        }
        return -EINVAL;
    }

    bool process::build_user_stack(char* const argv[], char* const envp[], u64* rsp_out) {
        if (address_space_ == nullptr || rsp_out == nullptr)
            return false;
        process*              caller = current_process();
        const arch::page_map* caller_map =
            caller == nullptr ? nullptr : caller->address_space();
        address_space_->activate();
        constexpr u64 stack_top   = 0x00007ffffff000ull;
        constexpr u64 stack_size  = 256 * 1024;
        constexpr u32 stack_pages = static_cast<u32>(stack_size / 4096);
        const u64     stack_base  = stack_top - stack_size;
        const u64 run = mm::kernel_frames().alloc(stack_pages, mm::frame_flags::zeroed);
        if (run == mm::frame_allocator::invalid_frame)
            return false;
        u64 frames[stack_pages]{};
        for (u32 i = 0; i < stack_pages; ++i) {
            frames[i] = run + u64{i} * 4096;
            if (!address_space_->map_4k(stack_base + u64{i} * 4096, frames[i],
                                        arch::page_flags::present |
                                            arch::page_flags::writable |
                                            arch::page_flags::user)) {
                // map_4k may have allocated intermediate page tables, but the
                // leaf frames are still ours.  Do not strand the whole run on
                // a partial stack construction failure.
                for (u32 done = 0; done < i; ++done)
                    address_space_->unmap(stack_base + u64{done} * 4096);
                mm::kernel_frames().release(run, stack_pages);
                if (caller_map != nullptr)
                    caller_map->activate();
                else
                    arch::kernel_page_map()->activate();
                return false;
            }
        }
        auto* region = mm::phys_region::adopt(run, stack_pages);
        if (region == nullptr) {
            // adopt() does not own the frame run when allocation of its
            // bookkeeping fails.
            for (u32 done = 0; done < stack_pages; ++done)
                address_space_->unmap(stack_base + u64{done} * 4096);
            mm::kernel_frames().release(run, stack_pages);
            if (caller_map != nullptr)
                caller_map->activate();
            else
                arch::kernel_page_map()->activate();
            return false;
        }
        if (!register_mapping(stack_base, stack_pages,
                              map_protection::read | map_protection::write, region)) {
            // register_mapping adopts the region only on success.
            region->release_ref();
            for (u32 done = 0; done < stack_pages; ++done)
                address_space_->unmap(stack_base + u64{done} * 4096);
            if (caller_map != nullptr)
                caller_map->activate();
            else
                arch::kernel_page_map()->activate();
            return false;
        }
        const char* argv_list[max_spawn_args]{};
        const char* envp_list[max_spawn_env]{};
        u32         argc = 0, envc = 0;
        while (argv != nullptr && argc < max_spawn_args && argv[argc] != nullptr)
            argv_list[argc] = argv[argc], ++argc;
        if (argc == 0)
            argv_list[argc++] = name_;
        while (envp != nullptr && envc < max_spawn_env && envp[envc] != nullptr)
            envp_list[envc] = envp[envc], ++envc;
        const bool result =
            build_initial_stack(stack_base, stack_top, frames, stack_pages, argv_list,
                                argc, envp_list, envc, rsp_out);
        if (caller_map != nullptr)
            caller_map->activate();
        else
            arch::kernel_page_map()->activate();
        return result;
    }

    bool process::exec_user_image(const void* bytes, usize length, char* const argv[],
                                  char* const envp[], u64* entry_out, u64* stack_out) {
        process staging(pid_, parent_pid_, name_);
        u64     entry = 0, stack = 0;
        if (!staging.load_user_image(bytes, length, &entry) ||
            !staging.build_user_stack(argv, envp, &stack))
            return false;
        arch::page_map* old_map     = address_space_;
        mmap_region*    old_regions = mmap_regions_;
        address_space_              = staging.address_space_;
        mmap_regions_               = staging.mmap_regions_;
        tls_tp_                     = staging.tls_tp_;
        staging.address_space_      = nullptr;
        staging.mmap_regions_       = nullptr;
        staging.tls_tp_             = 0;
        if (address_space_ != nullptr)
            address_space_->activate();
        delete old_map;
        while (old_regions != nullptr) {
            mmap_region* next = old_regions->next;
            old_regions->backing->unmap_range(old_regions->region_page,
                                              old_regions->pages);
            if (old_regions->holds_ref)
                old_regions->backing->release_ref();
            delete old_regions;
            old_regions = next;
        }
        *entry_out = entry;
        *stack_out = stack;
        return true;
    }

    bool process::exec_user_file(const mm::shptr<fs::open_file>& file, usize length,
                                 char* const argv[], char* const envp[], u64* entry_out,
                                 u64* stack_out) {
        process staging(pid_, parent_pid_, name_);
        u64     entry = 0, stack = 0;
        if (!staging.load_user_file(file, length, &entry) ||
            !staging.build_user_stack(argv, envp, &stack))
            return false;
        close_on_exec_descriptors();
        sys::io_exec_process(this);
        arch::page_map* old_map     = address_space_;
        mmap_region*    old_regions = mmap_regions_;
        address_space_              = staging.address_space_;
        mmap_regions_               = staging.mmap_regions_;
        tls_tp_                     = staging.tls_tp_;
        staging.address_space_      = nullptr;
        staging.mmap_regions_       = nullptr;
        staging.tls_tp_             = 0;
        if (address_space_ != nullptr)
            address_space_->activate();
        delete old_map;
        while (old_regions != nullptr) {
            mmap_region* next = old_regions->next;
            old_regions->backing->unmap_range(old_regions->region_page,
                                              old_regions->pages);
            if (old_regions->holds_ref)
                old_regions->backing->release_ref();
            delete old_regions;
            old_regions = next;
        }
        *entry_out = entry;
        *stack_out = stack;
        return true;
    }

    mm::shptr<thread> process::create_user_thread(u64 entry, char* const argv[],
                                                  char* const envp[]) {
        if (address_space_ == nullptr) {
            return {};
        }
        constexpr u64 stack_top   = 0x00007ffffff000ull;
        constexpr u64 stack_size  = 256 * 1024;
        constexpr u32 stack_pages = static_cast<u32>(stack_size / 4096);
        u64           frames[stack_pages]{};
        const u64     stack_base = stack_top - stack_size;
        // One contiguous run, so the stack's VMA can adopt it as a
        // single phys_region — the unit fork's copy-on-write
        // accounting works in.
        process*              caller = current_process();
        const arch::page_map* caller_map =
            caller == nullptr ? nullptr : caller->address_space();
        address_space_->activate();
        const u64 run = mm::kernel_frames().alloc(stack_pages, mm::frame_flags::zeroed);
        if (run == mm::frame_allocator::invalid_frame) {
            return {};
        }
        for (u32 i = 0; i < stack_pages; ++i) {
            frames[i] = run + u64{i} * 4096;
            if (!address_space_->map_4k(stack_base + u64{i} * 4096, frames[i],
                                        arch::page_flags::present |
                                            arch::page_flags::writable |
                                            arch::page_flags::user)) {
                return {};
            }
        }
        auto* stack_region = mm::phys_region::adopt(run, stack_pages);
        if (stack_region == nullptr ||
            !register_mapping(stack_base, stack_pages,
                              map_protection::read | map_protection::write,
                              stack_region)) {
            if (stack_region != nullptr) {
                stack_region->release();
            }
            return {};
        }

        // Argument/environment content for the startup frame.  An
        // empty argv defaults to argv[0] = the image path, matching
        // the execve contract that argv is never empty.
        const char* argv_list[max_spawn_args]{};
        const char* envp_list[max_spawn_env]{};
        u32         argc = 0;
        u32         envc = 0;
        while (argv != nullptr && argc < max_spawn_args && argv[argc] != nullptr) {
            argv_list[argc] = argv[argc];
            ++argc;
        }
        if (argc == 0) {
            argv_list[argc++] = name_;
        }
        while (envp != nullptr && envc < max_spawn_env && envp[envc] != nullptr) {
            envp_list[envc] = envp[envc];
            ++envc;
        }

        u64 user_entry_stack = stack_top;
        if (!build_initial_stack(stack_top - stack_size, stack_top, frames, stack_pages,
                                 argv_list, argc, envp_list, envc, &user_entry_stack)) {
            return {};
        }

        auto value = mm::share<thread>(this, proc::allocate_tid());
        if (!value || !value->prepare_user(entry, user_entry_stack, tls_tp_) ||
            !add_thread(value)) {
            return {};
        }
        if (!scheduler::wake(value.get())) {
            release_thread(value.get());
            return {};
        }
        if (caller_map != nullptr)
            caller_map->activate();
        else
            arch::kernel_page_map()->activate();
        return value;
    }

    mm::shptr<thread> process::create_thread(u64 entry, u64 user_stack_top, u64 argument,
                                             u64 argument1) {
        if (address_space_ == nullptr || entry == 0 || user_stack_top == 0) {
            return {};
        }
        const tid_t id        = proc::allocate_tid();
        u64         fs_base   = tls_tp_;
        u64         tls_base  = 0;
        usize       tls_pages = 0;
        if (tls_tp_ != 0 && !clone_tls_for_thread(id, fs_base, tls_base, tls_pages)) {
            return {};
        }
        auto value = mm::share<thread>(this, id);
        if (!value ||
            !value->prepare_user_start(entry, user_stack_top, fs_base, argument,
                                       argument1) ||
            !add_thread(value)) {
            return {};
        }
        if (!scheduler::wake(value.get())) {
            release_thread(value.get());
            return {};
        }
        value->tls_mapping(tls_base, tls_pages);
        return value;
    }

    bool process::clone_tls_for_thread(tid_t id, u64& fs_base, u64& mapping_base,
                                       usize& mapping_pages) {
        if (tls_tp_ == 0) {
            mapping_base  = 0;
            mapping_pages = 0;
            return true;
        }
        const u64    page_bytes = mm::phys_region::frame_size;
        mmap_region* source     = nullptr;
        for (mmap_region* it = mmap_regions_; it != nullptr; it = it->next) {
            if (it->covers(tls_tp_ - 1) && it->backing != nullptr) {
                source = it;
                break;
            }
        }
        if (source == nullptr) {
            return false;
        }

        u64  base = process_tls_first -
                    (static_cast<u64>(id % max_process_threads) * process_tls_stride);
        bool free = false;
        for (u32 attempt = 0; attempt < max_process_threads; ++attempt) {
            free = true;
            for (mmap_region* it = mmap_regions_; it != nullptr; it = it->next) {
                if (base < it->end() && it->base < base + source->size()) {
                    free = false;
                    break;
                }
            }
            if (free) {
                break;
            }
            base -= process_tls_stride;
        }
        if (!free) {
            return false;
        }
        auto* copy = mm::phys_region::create(source->pages, false);
        if (copy == nullptr) {
            return false;
        }
        for (usize page = 0; page < source->pages; ++page) {
            __builtin_memcpy(copy->page_direct(page), source->backing->page_direct(page),
                             page_bytes);
            if (!address_space_->map_4k(
                    base + page * page_bytes, copy->base() + page * page_bytes,
                    arch::page_flags::present | arch::page_flags::writable |
                        arch::page_flags::user)) {
                for (usize done = 0; done < page; ++done) {
                    address_space_->unmap(base + done * page_bytes);
                }
                copy->release();
                return false;
            }
        }
        if (!register_mapping(base, source->pages,
                              map_protection::read | map_protection::write, copy)) {
            for (usize page = 0; page < source->pages; ++page) {
                address_space_->unmap(base + page * page_bytes);
            }
            copy->release();
            return false;
        }
        mapping_base  = base;
        mapping_pages = source->pages;
        fs_base       = base + (tls_tp_ - source->base);
        auto* tcb     = static_cast<u8*>(
            copy->page_direct(static_cast<usize>((fs_base - base) / page_bytes)));
        *reinterpret_cast<u64*>(tcb + (fs_base - base) % page_bytes) = fs_base;
        return true;
    }

    mm::shptr<thread> process::thread_by_tid(tid_t id) {
        core::spinlock_guard guard(process_lock);
        for (u32 i = 0; i < max_process_threads; ++i) {
            if (threads_[i] && threads_[i]->tid() == id) {
                return threads_[i];
            }
        }
        return {};
    }

    bool process::copy_descriptors_from(const process& other) {
        for (u32 fd = 0; fd < max_process_fds; ++fd) {
            auto entry = other.descriptors_.get(fd_t{static_cast<i32>(fd)});
            auto copy =
                entry ? mm::share<fs::open_file>(*entry) : mm::shptr<fs::open_file>{};
            if (entry && (!copy || !descriptors_.set(fd_t{static_cast<i32>(fd)}, copy,
                                                     other.descriptors_.flags(
                                                         fd_t{static_cast<i32>(fd)})))) {
                return false;
            }
        }
        return true;
    }

    mm::shptr<thread> process::create_fork_thread(const arch::user_context& uctx) {
        auto value = mm::share<thread>(this, proc::allocate_tid());
        if (!value || !value->prepare_fork_user(uctx)) {
            return {};
        }
        if (!add_thread(value)) {
            return {};
        }
        if (!scheduler::wake(value.get())) {
            release_thread(value.get());
            return {};
        }
        return value;
    }

} // namespace astia::proc
