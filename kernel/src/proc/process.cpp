#include "process.hpp"

#include <astia/api/signal.h>
#include <astia/api/syscalls.h>
#include <astia/api/termios.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/spinlock_guard.hpp>
#include <astia/atomic.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/share.hpp>

#include "../fs/path_service.hpp"
#include "../ipc/futex.hpp"
#include "../sys/aio.hpp"
#include "events.hpp"
#include "exec_hooks.hpp"
#include "proc.hpp"
#include "scheduler.hpp"

void* operator new[](astia::usize size, void* where) noexcept;

namespace astia::proc {
    i64 process::change_directory(const char* path) {
        return _cwd.change_directory(path);
    }

    i64 process::change_directory(const fs::open_file& directory) {
        if (!directory.mode().is_directory())
            return -ENOTDIR;

        const char* path = directory.path();
        if (path == nullptr)
            return -ENOENT;

        const u64 inode = directory.parent() ? directory.parent()->inode() : 0;

        return _cwd.bind_directory(path,
                                   directory_anchor{directory.mount_reference(), inode});
    }

    i64 process::copy_cwd(span<char> buffer) const {
        return _cwd.copy_cwd(buffer);
    }

    i64 process::compose_path(const char* path, span<char> buffer) const {
        return _cwd.compose_path(path, buffer);
    }

    i64 process::compose_path_at(fd_t dirfd, const char* path, span<char> buffer) const {
        if (path == nullptr || buffer.null() || buffer.empty())
            return -EINVAL;

        if (path[0] == '/')
            return _cwd.compose_path(path, buffer);

        if (dirfd.value == AT_FDCWD)
            return _cwd.compose_path(path, buffer);

        auto directory = descriptor(dirfd);
        if (!directory)
            return -EBADF;

        if (!directory->path() || !directory->mode().is_directory())
            return -ENOTDIR;

        return _cwd.compose_path_at(directory->path(), path, buffer);
    }

    i64 process::resolve_path(const char* path, span<char> buffer,
                              bool follow_final) const {
        return _cwd.resolve_path(path, buffer, follow_final);
    }

    i64 process::resolve_path_at(fd_t dirfd, const char* path, span<char> buffer,
                                 bool follow_final) const {
        if (path == nullptr || buffer.null() || buffer.empty())
            return -EINVAL;

        if (path[0] == '/' || dirfd.value == AT_FDCWD)
            return _cwd.resolve_path(path, buffer, follow_final);

        auto directory = descriptor(dirfd);
        if (!directory)
            return -EBADF;

        if (!directory->path() || !directory->mode().is_directory())
            return -ENOTDIR;

        if (!_cwd.within_root(directory->path()))
            return -ENOENT;

        return _cwd.resolve_path_at(directory->path(), path, buffer, follow_final);
    }

    i64 process::chroot(const char* path) {
        if (euid() != 0)
            return -EPERM;

        if (path == nullptr || path[0] == '\0')
            return -ENOENT;

        char      canonical[fs::path_max]{};
        const i64 resolve_rc =
            _cwd.resolve_path(path, span<char>(canonical, sizeof canonical), true);
        if (resolve_rc < 0)
            return resolve_rc;

        const fs::file_type type = fs::node_type(canonical);

        if (type == fs::directory_unknown)
            return -ENOENT;

        if (type != fs::directory_dir)
            return -ENOTDIR;

        const fs::file_mode mode = fs::node_mode(canonical);

        if (mode.exists() &&
            !can_access(mode, fs::node_uid(canonical), fs::node_gid(canonical),
                        fs::permission_bits::execute))
            return -EACCES;

        auto handle = fs::open_directory(canonical);
        if (!handle)
            return -ENOENT;

        return _cwd.change_root(canonical,
                                static_cast<mm::shptr<fs::open_file>&&>(handle));
    }

    namespace {
        // Process-owned thread and lifecycle state stays on each process's
        // lock.  The waitpid/signal waiter lists have their own IRQ-safe lock: their
        // register-before-scan protocol must remain atomic with interrupts
        // masked, but they do not need to serialize with the thread table.
        core::spinlock process_wait_lock;
        core::spinlock dead_thread_lock;

        inline constexpr u32 dead_capacity = 16;
        // Placement-constructed like the process registry: keeps the
        // global trivially destructible in freestanding code.
        alignas(mm::shptr<thread>) u8
            dead_storage[sizeof(mm::shptr<thread>) * dead_capacity];
        mm::shptr<thread>* dead_queue = nullptr;
        u32                dead_count = 0;

        inline constexpr u32 dead_process_capacity = max_processes * 4;
        alignas(mm::shptr<process>) u8
            dead_process_storage[sizeof(mm::shptr<process>) * dead_process_capacity];
        mm::shptr<process>* dead_process_queue = nullptr;
        u32                 dead_process_count = 0;
    }

    /// Wake a thread that may be parked in an async wait.  The scheduler
    /// wake is still needed for threads parked on a completion, while the
    /// deposit resumes a futex/signal/task waiter on its own stack.
    void wake_blocked(thread* value) {
        if (value == nullptr)
            return;

        (void) value->wake_wait();
        (void) scheduler::wake(value);
    }

    void defer_dead_thread(const mm::shptr<thread>& value) {
        if (!value)
            return;

        arch::interrupt_spinlock_guard guard(dead_thread_lock);

        if (dead_queue == nullptr)
            dead_queue = ::new (dead_storage) mm::shptr<thread>[dead_capacity];

        if (dead_count == dead_capacity) {
            return; // queue full: the thread-table ref keeps it alive
        }

        dead_queue[dead_count++] = value;
    }

    // Reap queued dead threads.  Called from the scheduler only
    // after switching away from a context, where no queued thread
    // can still be running on its own stack.
    void reap_dead_threads() {
        if (dead_queue == nullptr)
            return;

        mm::shptr<thread> pending[dead_capacity];

        u32 count = 0;

        {
            arch::interrupt_spinlock_guard guard(dead_thread_lock);

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

    bool defer_dead_process(const mm::shptr<process>& value) {
        if (!value)
            return false;

        arch::interrupt_spinlock_guard guard(dead_thread_lock);

        if (dead_process_queue == nullptr)
            dead_process_queue =
                ::new (dead_process_storage) mm::shptr<process>[dead_process_capacity];

        if (dead_process_count == dead_process_capacity)
            return false;

        dead_process_queue[dead_process_count++] = value;

        return true;
    }

    void reap_dead_processes() {
        if (dead_process_queue == nullptr)
            return;

        mm::shptr<process> pending[dead_process_capacity];

        u32 count = 0;

        {
            arch::interrupt_spinlock_guard guard(dead_thread_lock);

            for (u32 i = 0; i < dead_process_count; ++i) {
                pending[i] = static_cast<mm::shptr<process>&&>(dead_process_queue[i]);
                dead_process_queue[i] = {};
            }

            count              = dead_process_count;
            dead_process_count = 0;
        }

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

        bool write_user(process* owner, u64 base, u64 top, u64 vaddr, const void* src,
                        usize length) {
            const auto* bytes = static_cast<const u8*>(src);

            while (length != 0) {
                if (vaddr < base || vaddr >= top || length > top - vaddr)
                    return false;

                const u64 page = vaddr & ~mm::page_mask;

                if (owner == nullptr || owner->address_space() == nullptr ||
                    (owner->address_space()->translate(page) ==
                         mm::address_space::invalid &&
                     !owner->cow_fault(page, false)))
                    return false;

                const u64 phys = owner->address_space()->translate(page);

                auto* target = static_cast<u8*>(mm::kernel_frames().to_virt(phys));
                if (target == nullptr)
                    return false;

                const usize within = static_cast<usize>(vaddr - page);
                usize       chunk  = mm::frame_allocator::frame_size - within;
                if (chunk > length)
                    chunk = length;

                for (usize i = 0; i < chunk; ++i) {
                    target[within + i] = bytes[i];
                }

                bytes += chunk;
                vaddr += chunk;
                length -= chunk;
            }

            return true;
        }

        bool write_user_u64(process* owner, u64 base, u64 top, u64 vaddr, u64 value) {
            return write_user(owner, base, top, vaddr, &value, sizeof(value));
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
        bool build_initial_stack(process* owner, u64 base, u64 top,
                                 const char* const* argv, u32 argc,
                                 const char* const* envp, u32 envc, u64* rsp_out) {
            u64 cursor = top;
            u64 argv_address[max_argv_entries]{};
            u64 envp_address[max_env_entries]{};

            // Strings first, packed from the top down.
            for (u32 i = envc; i > 0; --i) {
                const char* text = envp[i - 1];

                const usize length = cstr_length(text, max_spawn_string_bytes);

                if (cursor < base + length + 1)
                    return false;

                cursor -= length + 1;

                if (!write_user(owner, base, top, cursor, text, length) ||
                    !write_user(owner, base, top, cursor + length, "", 1))
                    return false;

                envp_address[i - 1] = cursor;
            }

            for (u32 i = argc; i > 0; --i) {
                const char* text = argv[i - 1];

                const usize length = cstr_length(text, max_spawn_string_bytes);

                if (cursor < base + length + 1)
                    return false;

                cursor -= length + 1;

                if (!write_user(owner, base, top, cursor, text, length) ||
                    !write_user(owner, base, top, cursor + length, "", 1))
                    return false;

                argv_address[i - 1] = cursor;
            }

            // The startup block: argc + argv[] + NULL + envp[] + NULL
            // + the interpreter-relevant auxv pairs + AT_NULL.  The auxv
            // size is derived from the array so adding a pair cannot overrun
            // the block into the argument strings.
            const auto info = owner->exec_image();

            // A spawn always starts from a freshly loaded image; never emit
            // auxv from a process that has no image.
            if (info.entry == 0)
                return false;

            const struct {
                u64 type;
                u64 value;
            } auxv[] = {
                {3, info.phdr},         // AT_PHDR
                {4, info.phent},        // AT_PHENT
                {5, info.phnum},        // AT_PHNUM
                {7, info.interp},       // AT_BASE
                {9, info.entry},        // AT_ENTRY
                {0x41, info.tls_base},  // AT_ASTIA_TLS_BASE
                {0x42, info.tls_tp},    // AT_ASTIA_TLS_TP
                {0x43, info.tls_limit}, // AT_ASTIA_TLS_LIMIT
                {0x44, info.base},      // AT_ASTIA_PAYLOAD_BASE
            };

            const u64 block =
                sizeof(u64) * (1 + argc + 1 + envc + 1) + sizeof(auxv) + 2 * sizeof(u64);

            if (cursor < base + block)
                return false;

            cursor = (cursor - block) & ~u64{15};
            if (cursor < base)
                return false;

            u64 at = cursor;

            if (!write_user_u64(owner, base, top, at, argc))
                return false;

            at += sizeof(u64);

            for (u32 i = 0; i < argc; ++i, at += sizeof(u64)) {
                if (!write_user_u64(owner, base, top, at, argv_address[i]))
                    return false;
            }

            if (!write_user_u64(owner, base, top, at, 0)) { // argv NULL
                return false;
            }

            at += sizeof(u64);

            for (u32 i = 0; i < envc; ++i, at += sizeof(u64)) {
                if (!write_user_u64(owner, base, top, at, envp_address[i]))
                    return false;
            }

            if (!write_user_u64(owner, base, top, at, 0)) { // envp NULL
                return false;
            }

            at += sizeof(u64);

            for (const auto& entry : auxv) {
                if (!write_user_u64(owner, base, top, at, entry.type) ||
                    !write_user_u64(owner, base, top, at + sizeof(u64), entry.value))
                    return false;

                at += sizeof(u64) * 2;
            }

            // AT_NULL terminator (type = 0, value = 0).
            if (!write_user_u64(owner, base, top, at, 0) ||
                !write_user_u64(owner, base, top, at + sizeof(u64), 0))
                return false;

            *rsp_out = cursor;

            return true;
        }
    } // namespace

    process::process(pid_t id, pid_t parent, const char* name)
        : _pid(id), _parent_pid(parent), _process_group(pgrp_from_raw(raw_id(id))),
          _session(id) {
        u32 i = 0;

        while (name != nullptr && name[i] != '\0' && i + 1 < sizeof(_name)) {
            _name[i] = name[i];

            ++i;
        }

        _name[i] = '\0';
    }

    namespace {
        const char* process_state_name(process_state state) {
            switch (state) {
            case process_state::fresh:
                return "fresh";
            case process_state::runnable:
                return "runnable";
            case process_state::stopping:
                return "stopping";
            case process_state::stopped:
                return "stopped";
            case process_state::zombie:
                return "zombie";
            case process_state::dead:
                return "dead";
            }

            return "unknown";
        }
    } // namespace

    void process::panic_snapshot(dbg::panic_process& out) const {
        out.available = true;
        out.pid       = static_cast<u32>(raw_id(_pid));
        out.state     = process_state_name(state());

        u32 i = 0;

        while (i + 1 < sizeof out.name && _name[i] != '\0') {
            out.name[i] = _name[i];

            ++i;
        }

        out.name[i] = '\0';

        if (_metadata_lock.try_lock()) {
            out.metadata_available = true;
            out.uid                = _uid;
            out.euid               = _euid;
            out.gid                = _gid;
            out.egid               = _egid;
            out.pgrp               = static_cast<u32>(raw_id(_process_group));
            out.session            = static_cast<u32>(raw_id(_session));

            _metadata_lock.unlock();
        }
    }

    i64 process::setuid(u32 v) {
        arch::interrupt_spinlock_guard guard(_metadata_lock);

        if (_euid != 0 && v != _uid && v != _euid)
            return -EPERM;

        _uid = _euid = v;

        return 0;
    }

    i64 process::seteuid(u32 v) {
        arch::interrupt_spinlock_guard guard(_metadata_lock);

        if (_euid != 0 && v != _uid && v != _euid)
            return -EPERM;

        _euid = v;

        return 0;
    }

    i64 process::setgid(u32 v) {
        arch::interrupt_spinlock_guard guard(_metadata_lock);

        if (_egid != 0 && v != _gid && v != _egid)
            return -EPERM;

        _gid = _egid = v;

        return 0;
    }

    i64 process::setegid(u32 v) {
        arch::interrupt_spinlock_guard guard(_metadata_lock);

        if (_egid != 0 && v != _gid && v != _egid)
            return -EPERM;

        _egid = v;

        return 0;
    }

    i64 process::setreuid(u32 r, u32 e) {
        arch::interrupt_spinlock_guard guard(_metadata_lock);

        if (_euid != 0 && r != _uid && r != _euid && e != _uid && e != _euid)
            return -EPERM;

        if (r != static_cast<u32>(-1))
            _uid = r;

        if (e != static_cast<u32>(-1))
            _euid = e;

        return 0;
    }

    i64 process::setregid(u32 r, u32 e) {
        arch::interrupt_spinlock_guard guard(_metadata_lock);

        if (_egid != 0 && r != _gid && r != _egid && e != _gid && e != _egid)
            return -EPERM;

        if (r != static_cast<u32>(-1))
            _gid = r;

        if (e != static_cast<u32>(-1))
            _egid = e;

        return 0;
    }

    bool process::in_group(gid_t group) const {
        arch::interrupt_spinlock_guard guard(_metadata_lock);

        if (_egid == static_cast<u32>(group))
            return true;

        for (u32 i = 0; i < _group_count; ++i) {
            if (_groups[i] == group)
                return true;
        }

        return false;
    }

    i64 process::getgroups(i32 count, u32* out) const {
        if (count < 0 || (count != 0 && out == nullptr))
            return -EINVAL;

        u32 snapshot[max_supplementary_groups]{};
        u32 snapshot_count = 0;

        {
            arch::interrupt_spinlock_guard guard(_metadata_lock);

            snapshot_count = _group_count;

            if (count != 0 && static_cast<u32>(count) < snapshot_count)
                return -EINVAL;

            for (u32 i = 0; i < snapshot_count; ++i) {
                snapshot[i] = static_cast<u32>(_groups[i]);
            }
        }

        if (count == 0)
            return snapshot_count;

        for (u32 i = 0; i < snapshot_count; ++i) {
            out[i] = snapshot[i];
        }

        return snapshot_count;
    }

    i64 process::setgroups(i32 count, const u32* values) {
        if (count < 0 || static_cast<u32>(count) > max_supplementary_groups ||
            (count != 0 && values == nullptr))
            return -EINVAL;

        gid_t snapshot[max_supplementary_groups]{};

        {
            arch::interrupt_spinlock_guard guard(_metadata_lock);

            if (_euid != 0)
                return -EPERM;
        }

        for (u32 i = 0; i < static_cast<u32>(count); ++i) {
            snapshot[i] = gid_t{values[i]};
        }

        arch::interrupt_spinlock_guard guard(_metadata_lock);

        if (_euid != 0)
            return -EPERM;

        _group_count = static_cast<u32>(count);

        for (u32 i = 0; i < _group_count; ++i) {
            _groups[i] = snapshot[i];
        }

        return 0;
    }

    process::~process() {
        // VMA teardown releases frame references under spinlocks. Do not let
        // the timer switch this CPU while the dying process owns one: the
        // next thread could attempt the same lock and deadlock permanently.
        arch::interrupt_guard gate;

        futex_remove_process(this);
        discard_image();
        events::teardown::emit(*this);
        // Member destructors run after this body, which would otherwise
        // restore IF before their shptr releases. Empty them while the gate
        // is still closed.
        _descriptors.clear_all();

        for (u32 i = 0; i < max_threads_per_process; ++i) {
            _threads[i] = {};
        }
    }

    void process::discard_image() {
        while (_mmap_regions != nullptr) {
            mmap_region* victim = _mmap_regions;

            _mmap_regions = victim->next;

            victim->backing->unmap_range(victim->region_page, victim->pages);

            if (victim->holds_ref)
                victim->backing->release_ref();

            delete victim;
        }

        _tls_tp       = 0;
        _mapped_bytes = 0;
    }

    bool process::add_thread(const mm::shptr<thread>& value) {
        if (!value)
            return false;

        arch::interrupt_spinlock_guard guard(_lifecycle_lock);

        for (u32 i = 0; i < max_threads_per_process; ++i) {
            if (!_threads[i]) {
                _threads[i] = value;

                ++_thread_count;

                return true;
            }
        }

        return false;
    }

    mm::shptr<thread> process::thread_at(u32 index) const {
        return index < max_threads_per_process ? _threads[index] : mm::shptr<thread>{};
    }

    void process::request_exit(thread* caller, i32 status) {
        thread* wake_list[max_threads_per_process];

        u32 count = 0;

        {
            arch::interrupt_spinlock_guard guard(_lifecycle_lock);

            if (!process_exit_requested()) {
                astia::atomic_ref<i32>(_requested_exit_status)
                    .store(status, astia::memory_order::release);
                astia::atomic_ref<u8>(_exit_requested)
                    .store(1, astia::memory_order::release);
            }

            for (u32 i = 0; i < max_threads_per_process; ++i) {
                thread* value = _threads[i].get();

                if (value == nullptr || value == caller ||
                    value->state() == thread_state::dead)
                    continue;

                value->request_exit();
                value->request_cancellation();
                (void) value->wake_wait();

                if (value->state() == thread_state::blocked)
                    wake_list[count++] = value;
            }
        }

        futex_remove_process(this);
        sys::io_interrupt_process(this, -EINTR);

        for (u32 i = 0; i < count; ++i) {
            wake_blocked(wake_list[i]);
        }
    }

    void process::terminate_for_signal(signal_number signal, thread* caller) {
        signal_termination(signal);
        request_exit(caller, wait_signal_exit_status(static_cast<u8>(signal)));
    }

    bool process::thread_exited(thread* value, i32 status) {
        if (value != nullptr) {
            release_thread_tls(value);

            if (value->detached())
                release_thread_stack(value);
        }

        {
            arch::interrupt_spinlock_guard guard(_lifecycle_lock);

            if (state() == process_state::zombie || state() == process_state::dead)
                return false;

            for (u32 i = 0; i < max_threads_per_process; ++i) {
                if (_threads[i] && _threads[i].get() != value &&
                    _threads[i]->state() != thread_state::dead)
                    return false;
            }

            _exit_status = process_exit_requested() ? _requested_exit_status : status;
        }

        bool discard_status = false;

        if (auto parent = retain_process(_parent_pid)) {
            const auto disposition = parent->signals().disposition(signal_number::chld);

            discard_status = (disposition.flags & ASTIA_SA_NOCLDWAIT) != 0;
        }

        mm::shptr<process> deferred;

        if (discard_status) {
            deferred = retain_process(_pid);
            if (!deferred) {
                discard_status = false;
            }
            else {
                if (!defer_dead_process(deferred))
                    discard_status = false;
            }
        }

        {
            arch::interrupt_spinlock_guard guard(_lifecycle_lock);

            state(discard_status ? process_state::dead : process_state::zombie);
        }

        // Closing a session leader's terminal hangs up its foreground job.
        // Do this before dropping descriptors so the terminal's foreground
        // process group is still available to the signal fan-out.
        metadata_snapshot metadata{};

        snapshot_metadata(metadata);

        if (metadata.controlling_terminal && metadata.session == _pid) {
            (void) signal_process_group(pgrp_from_raw(raw_id(metadata.process_group)),
                                        signal_number::hup);
            controlling_terminal({});
        }

        // Retire every token created under this process.  Subsystems see it
        // through their child sources; AIO uses it to abort in-flight ops.
        _cancellation_root.cancel(core::cancel_reason::shutdown);
        // Release a child's private ring at exit rather than waiting for
        // waitpid: the parent and subsequent children may still need slots.
        sys::io_exit_process(this);
        // A dead process no longer owns any descriptors.  Release them after
        // its ring is quiesced: a pipe writer left in a zombie would otherwise
        // keep readers blocked until the parent reaps it.
        _descriptors.clear_all();

        // Waiters block on the *parent* (registered by proc::waitpid);
        // waking must happen on that object, not on the dying process.
        signal_info info{};

        info.code   = _termination_signal != 0 ? ASTIA_CLD_KILLED : ASTIA_CLD_EXITED;
        info.pid    = static_cast<i32>(raw_id(_pid));
        info.uid    = uid();
        info.status = _termination_signal != 0 ? _termination_signal : _exit_status;

        if (discard_status && unregister_process(this)) {
            notify_parent_of_state_change(info);

            return true;
        }

        if (discard_status) {
            arch::interrupt_spinlock_guard guard(_lifecycle_lock);

            state(process_state::zombie);
        }

        notify_parent_of_state_change(info);

        return false;
    }

    void process::notify_parent_of_state_change(signal_info info) {
        mm::shptr<process> parent = retain_process(_parent_pid);
        if (!parent)
            return;

        info.signo = static_cast<i32>(signal_number::chld);

        const bool stop_or_continue =
            info.code == ASTIA_CLD_STOPPED || info.code == ASTIA_CLD_CONTINUED;

        const auto disposition = parent->signals().disposition(signal_number::chld);

        if (!stop_or_continue || (disposition.flags & ASTIA_SA_NOCLDSTOP) == 0) {
            (void) parent->signals().send(signal_number::chld, info);

            if (parent->address_space() != nullptr)
                parent->wake_signal_waiters();
        }

        parent->wake_waiters();
    }

    void process::signal_state_change(signal_number signal) {
        if (signal == signal_number::cont || signal == signal_number::kill) {
            thread* wake_list[max_threads_per_process];

            u32 count = 0;

            {
                arch::interrupt_spinlock_guard guard(_lifecycle_lock);

                if (state() != process_state::stopped)
                    return;

                state(process_state::runnable);

                if (signal == signal_number::cont)
                    astia::atomic_ref<u8>(_continued_event)
                        .store(1, astia::memory_order::release);

                for (u32 i = 0; i < max_threads_per_process; ++i) {
                    thread* value = _threads[i].get();

                    if (value != nullptr && value->state() == thread_state::blocked)
                        wake_list[count++] = value;
                }
            }

            for (u32 i = 0; i < count; ++i) {
                wake_blocked(wake_list[i]);
            }

            signal_info info{};

            info.code   = ASTIA_CLD_CONTINUED;
            info.pid    = static_cast<i32>(raw_id(_pid));
            info.uid    = uid();
            info.status = static_cast<i32>(signal_number::cont);

            notify_parent_of_state_change(info);

            return;
        }

        if (signal != signal_number::stop && signal != signal_number::tstp &&
            signal != signal_number::ttin && signal != signal_number::ttou)
            return;

        {
            arch::interrupt_spinlock_guard guard(_lifecycle_lock);

            if (state() == process_state::zombie || state() == process_state::dead ||
                state() == process_state::stopped)
                return;

            _stop_signal = static_cast<i32>(signal);

            astia::atomic_ref<u8>(_stop_event).store(1, astia::memory_order::release);
            state(process_state::stopped);

            for (u32 i = 0; i < max_threads_per_process; ++i) {
                thread* value = _threads[i].get();

                if (value != nullptr && value->state() != thread_state::dead)
                    value->state(thread_state::blocked);
            }
        }

        signal_info info{};

        info.code   = ASTIA_CLD_STOPPED;
        info.pid    = static_cast<i32>(raw_id(_pid));
        info.uid    = uid();
        info.status = static_cast<i32>(signal);

        notify_parent_of_state_change(info);
    }

    void process::request_kill() {
        thread* wake_list[max_threads_per_process];

        u32 count = 0;

        {
            arch::interrupt_spinlock_guard guard(_lifecycle_lock);

            if (state() == process_state::zombie || state() == process_state::dead)
                return;

            if (state() == process_state::stopped)
                state(process_state::runnable);

            for (u32 i = 0; i < max_threads_per_process; ++i) {
                thread* value = _threads[i].get();

                if (value == nullptr || value->state() == thread_state::dead)
                    continue;

                value->request_kill();

                if (value->state() == thread_state::blocked)
                    wake_list[count++] = value;
            }
        }

        for (u32 i = 0; i < count; ++i) {
            wake_blocked(wake_list[i]);
        }
    }

    void process::interrupt_signal_io(signal_number signal) {
        const auto disposition = _signals.disposition(signal);

        const bool interrupt =
            disposition.action == signal_action::handler
                ? (disposition.flags & ASTIA_SA_RESTART) == 0
                : disposition.action == signal_action::default_action &&
                      default_action(signal) != signal_default::ignore;
        if (!interrupt)
            return;

        const signal_mask bit = signal_bit(signal);

        for (u32 i = 0; i < max_threads_per_process; ++i) {
            const auto value = thread_at(i);

            if (value && (value->signals().blocked() & bit) == 0) {
                sys::io_interrupt_process(this, -EINTR);
                (void) futex_interrupt_process(this, signal);

                return;
            }
        }
    }

    i64 process::alarm(u32 seconds, u64 now, u32 hz) {
        u64 next = 0;

        if (seconds != 0) {
            const u64 duration = static_cast<u64>(seconds) * hz;

            next = UINT64_MAX - now < duration ? UINT64_MAX : now + duration;
        }

        const u64 previous = astia::atomic_ref<u64>(_alarm_deadline)
                                 .exchange(next, astia::memory_order::acq_rel);

        if (previous == 0 || previous <= now)
            return 0;

        const u64 remaining = previous - now;

        return static_cast<i64>((remaining + hz - 1) / hz);
    }

    void process::expire_alarm(u64 now) {
        if (state() == process_state::zombie || state() == process_state::dead)
            return;

        u64 deadline =
            astia::atomic_ref<u64>(_alarm_deadline).load(astia::memory_order::acquire);

        while (deadline != 0 && deadline <= now) {
            if (astia::atomic_ref<u64>(_alarm_deadline)
                    .compare_exchange_strong(deadline, 0, astia::memory_order::acq_rel,
                                             astia::memory_order::acquire)) {
                (void) _signals.send(signal_number::alrm);
                interrupt_signal_io(signal_number::alrm);
                wake_signal_waiters();
                wake_waiters();

                return;
            }
        }
    }

    mm::shptr<thread> process::take_thread(thread* value) {
        arch::interrupt_spinlock_guard guard(_lifecycle_lock);

        for (u32 i = 0; i < max_threads_per_process; ++i) {
            if (_threads[i] && _threads[i].get() == value) {
                mm::shptr<thread> taken = _threads[i];

                _threads[i] = {};

                if (_thread_count != 0)
                    --_thread_count;

                return taken;
            }
        }

        return {};
    }

    mm::shptr<thread> process::retain_thread(thread* value) {
        arch::interrupt_spinlock_guard guard(_lifecycle_lock);

        for (u32 i = 0; i < max_threads_per_process; ++i) {
            if (_threads[i] && _threads[i].get() == value)
                return _threads[i];
        }

        return {};
    }

    void process::release_thread(thread* value) {
        mm::shptr<thread> released;

        {
            arch::interrupt_spinlock_guard guard(_lifecycle_lock);

            for (u32 i = 0; i < max_threads_per_process; ++i) {
                if (_threads[i] && _threads[i].get() == value) {
                    // Do not run ~thread while _lifecycle_lock is held.
                    released = static_cast<mm::shptr<thread>&&>(_threads[i]);

                    if (_thread_count != 0)
                        --_thread_count;

                    break;
                }
            }
        }
    }

    void process::release_thread_tls(thread* value) {
        if (value == nullptr || value->tls_mapping_base() == 0 ||
            value->tls_mapping_pages() == 0)
            return;

        const u64   base  = value->tls_mapping_base();
        const usize bytes = value->tls_mapping_pages() * mm::phys_region::frame_size;

        value->clear_tls_mapping();
        (void) unmap(base, bytes);
    }

    void process::release_thread_stack(thread* value) {
        if (value == nullptr || value->user_stack_top() == 0 ||
            !value->claim_owned_user_stack())
            return;

        const u64 stack_top = value->user_stack_top();

        value->clear_user_stack_metadata();

        u64   base  = 0;
        usize bytes = 0;

        {
            arch::interrupt_spinlock_guard guard(_mmap_lock);

            for (mmap_region* it = _mmap_regions; it != nullptr; it = it->next) {
                if (it->stack && it->covers(stack_top - 1)) {
                    base  = it->base;
                    bytes = static_cast<usize>(it->size());

                    break;
                }
            }
        }

        if (base != 0 && bytes != 0)
            (void) unmap(base, bytes);

        value->owns_user_stack(false);
    }

    bool process::add_waiter(thread* value) {
        if (value == nullptr)
            return false;

        arch::interrupt_spinlock_guard guard(process_wait_lock);

        for (u32 i = 0; i < max_waiters_per_process; ++i) {
            if (_waiters[i] == nullptr) {
                _waiters[i] = value;

                return true;
            }
        }

        return false;
    }

    void process::remove_waiter(thread* value) {
        arch::interrupt_spinlock_guard guard(process_wait_lock);

        for (u32 i = 0; i < max_waiters_per_process; ++i) {
            if (_waiters[i] == value)
                _waiters[i] = nullptr;
        }
    }

    void process::wake_waiters() {
        thread* wake_list[max_waiters_per_process];

        u32 count = 0;

        {
            arch::interrupt_spinlock_guard guard(process_wait_lock);

            for (u32 i = 0; i < max_waiters_per_process; ++i) {
                if (_waiters[i] != nullptr) {
                    wake_list[count++] = _waiters[i];
                    _waiters[i]        = nullptr;
                }
            }
        }

        for (u32 i = 0; i < count; ++i) {
            if (wake_list[i]->wake_wait())
                (void) scheduler::wake(wake_list[i]);
        }
    }

    bool process::add_signal_waiter(thread* value) {
        arch::interrupt_spinlock_guard guard(process_wait_lock);

        return _signal_waiters.add(value);
    }

    bool process::add_signal_waiter(thread* value, bool timed, u64 deadline) {
        arch::interrupt_spinlock_guard guard(process_wait_lock);

        return _signal_waiters.add(value, timed, deadline);
    }

    bool process::arm_signal_waiter(thread* value, std::coroutine_handle<> continuation) {
        arch::interrupt_spinlock_guard guard(process_wait_lock);

        if (_signal_waiters.consume_wake(value))
            return false;

        if (!value->arm_park_unless_cancelled(continuation))
            return false;

        return true;
    }

    void process::remove_signal_waiter(thread* value) {
        arch::interrupt_spinlock_guard guard(process_wait_lock);

        _signal_waiters.remove(value);
    }

    bool process::signal_waiter_timed_out(thread* value) {
        arch::interrupt_spinlock_guard guard(process_wait_lock);

        return _signal_waiters.timed_out(value);
    }

    void process::expire_signal_waiters(u64 now) {
        thread* wake_list[max_signal_waiters_per_process];

        u32 count;

        {
            arch::interrupt_spinlock_guard guard(process_wait_lock);

            count =
                _signal_waiters.expire(now, wake_list, max_signal_waiters_per_process);
        }

        for (u32 i = 0; i < count; ++i) {
            if (wake_list[i]->state() != thread_state::blocked)
                continue;

            if (wake_list[i]->wake_wait())
                (void) scheduler::wake(wake_list[i]);
        }
    }

    void process::wake_signal_waiters() {
        thread* wake_list[max_signal_waiters_per_process];

        u32 count;

        {
            arch::interrupt_spinlock_guard guard(process_wait_lock);

            count = _signal_waiters.wake(wake_list, max_signal_waiters_per_process);
        }

        for (u32 i = 0; i < count; ++i) {
            if (wake_list[i]->state() != thread_state::blocked)
                continue;

            if (wake_list[i]->wake_wait())
                (void) scheduler::wake(wake_list[i]);
        }
    }

    bool process::install_standard_streams() {
        const bool installed = _descriptors.install_standard_streams();
        if (installed) {
            auto tty = descriptor(fd_t{STDIN});
            if (tty) {
                controlling_terminal(tty);

                i32 group = static_cast<i32>(raw_id(process_group()));

                (void) tty->ioctl(TIOCSPGRP, &group, sizeof(i32));
            }
        }

        return installed;
    }

    mm::shptr<fs::open_file> process::descriptor(fd_t fd) const {
        return _descriptors.get(fd);
    }

    bool process::set_descriptor(fd_t fd, const mm::shptr<fs::open_file>& value) {
        astia_rlimit limit{};

        (void) get_rlimit(RLIMIT_NOFILE, &limit);

        if (fd.value < 0 || static_cast<u64>(fd.value) >= limit.current)
            return false;

        return _descriptors.set(fd, value);
    }

    bool process::set_descriptor(fd_t fd, const mm::shptr<fs::open_file>& value,
                                 u32 flags) {
        astia_rlimit limit{};

        (void) get_rlimit(RLIMIT_NOFILE, &limit);

        if (fd.value < 0 || static_cast<u64>(fd.value) >= limit.current)
            return false;

        return _descriptors.set(fd, value, flags);
    }

    optional<fd_t> process::allocate_descriptor(const mm::shptr<fs::open_file>& value,
                                                u32                             flags) {
        astia_rlimit limit{};

        (void) get_rlimit(RLIMIT_NOFILE, &limit);

        u32 maximum = static_cast<u32>(limit.current);
        if (maximum > max_process_fds)
            maximum = max_process_fds;

        return _descriptors.allocate(value, flags, maximum);
    }

    void process::close_on_exec_descriptors() {
        _descriptors.close_on_exec();
    }

    bool process::clear_descriptor(fd_t fd) {
        return _descriptors.clear(fd);
    }

    optional<fd_t> process::duplicate_descriptor(fd_t old_fd) {
        astia_rlimit limit{};

        (void) get_rlimit(RLIMIT_NOFILE, &limit);

        return _descriptors.duplicate(old_fd, fd_t{0}, 0,
                                      static_cast<u32>(limit.current));
    }

    optional<fd_t> process::duplicate_descriptor(fd_t old_fd, fd_t minimum, u32 flags) {
        astia_rlimit rlimit{};

        (void) get_rlimit(RLIMIT_NOFILE, &rlimit);

        const u32 limit = static_cast<u32>(rlimit.current);

        return _descriptors.duplicate(old_fd, minimum, flags, limit);
    }

    u32 process::descriptor_flags(fd_t fd) const {
        return _descriptors.flags(fd);
    }

    bool process::set_descriptor_flags(fd_t fd, u32 flags) {
        return _descriptors.set_flags(fd, flags);
    }

    optional<fd_t> process::duplicate_descriptor_to(fd_t old_fd, fd_t new_fd) {
        astia_rlimit limit{};

        (void) get_rlimit(RLIMIT_NOFILE, &limit);

        if (new_fd.value < 0 || static_cast<u64>(new_fd.value) >= limit.current)
            return error_t::emfile;

        return _descriptors.duplicate_to(old_fd, new_fd);
    }

    i64 process::get_rlimit(i32 resource, astia_rlimit* out) const {
        if (out == nullptr)
            return -EFAULT;

        arch::interrupt_spinlock_guard guard(_metadata_lock);

        if (resource == RLIMIT_NOFILE)
            *out = _rlimit_nofile;
        else if (resource == RLIMIT_AS)
            *out = _rlimit_as;
        else
            return -EINVAL;

        return 0;
    }

    i64 process::set_rlimit(i32 resource, const astia_rlimit& value) {
        arch::interrupt_spinlock_guard guard(_metadata_lock);

        if (value.current > value.maximum)
            return -EINVAL;

        if (resource == RLIMIT_NOFILE) {
            if (value.maximum > max_process_fds)
                return -EINVAL;

            if (value.maximum > _rlimit_nofile.maximum)
                return -EPERM;

            _rlimit_nofile = value;

            return 0;
        }

        if (resource == RLIMIT_AS) {
            if (value.maximum > _rlimit_as.maximum || value.current < _mapped_bytes)
                return value.maximum > _rlimit_as.maximum ? -EPERM : -EINVAL;

            _rlimit_as = value;

            return 0;
        }

        return -EINVAL;
    }

    bool process::build_user_stack(char* const argv[], char* const envp[], u64* rsp_out) {
        if (!_address_space.attached() || rsp_out == nullptr)
            return false;

        process*                 caller = current_process();
        const mm::address_space* caller_map =
            caller == nullptr ? nullptr : caller->address_space();

        _address_space.activate();

        const u64 stack_base = user_stack_top - user_stack_bytes;
        const u64 stack      = map_anonymous(
            stack_base, user_stack_bytes, map_protection::read | map_protection::write,
            map_flags::priv | map_flags::anonymous | map_flags::stack);
        if (stack != stack_base) {
            if (stack < user_address_limit)
                unmap(stack, user_stack_bytes);

            if (caller_map != nullptr)
                caller_map->activate();
            else
                mm::address_space::kernel().activate();

            return false;
        }

        const char* argv_list[max_argv_entries]{};
        const char* envp_list[max_env_entries]{};

        u32 argc = 0, envc = 0;

        while (argv != nullptr && argc < max_argv_entries && argv[argc] != nullptr) {
            argv_list[argc] = argv[argc], ++argc;
        }

        if (argc == 0)
            argv_list[argc++] = _name;

        while (envp != nullptr && envc < max_env_entries && envp[envc] != nullptr) {
            envp_list[envc] = envp[envc], ++envc;
        }

        const bool built = build_initial_stack(this, stack_base, user_stack_top,
                                               argv_list, argc, envp_list, envc, rsp_out);

        if (caller_map != nullptr)
            caller_map->activate();
        else
            mm::address_space::kernel().activate();

        if (!built) {
            // The VMA owns the frame run after register_mapping succeeds.
            // Remove it on a malformed or oversized startup vector.
            unmap(stack_base, user_stack_bytes);
        }

        return built;
    }

    void process::replace_image(process& staging) {
        mm::address_space old_map     = static_cast<mm::address_space&&>(_address_space);
        mmap_region*      old_regions = _mmap_regions;

        _address_space        = static_cast<mm::address_space&&>(staging._address_space);
        _mmap_regions         = staging._mmap_regions;
        _mapped_bytes         = staging._mapped_bytes;
        _tls_tp               = staging._tls_tp;
        _exec_image           = staging._exec_image;
        staging._mmap_regions = nullptr;
        staging._mapped_bytes = 0;
        staging._tls_tp       = 0;
        staging._exec_image   = {};

        if (_address_space.attached())
            _address_space.activate();

        while (old_regions != nullptr) {
            mmap_region* next = old_regions->next;

            old_regions->backing->unmap_range(old_regions->region_page,
                                              old_regions->pages);

            if (old_regions->holds_ref)
                old_regions->backing->release_ref();

            delete old_regions;

            old_regions = next;
        }
    }

    void process::reset_signal_stacks_for_exec() {
        for (u32 i = 0; i < max_threads_per_process; ++i) {
            const auto value = thread_at(i);
            if (value) {
                value->signal_stack(0, 0, ASTIA_SS_DISABLE);
                value->signal_stack_on_alt(false);
            }
        }
    }

    bool process::exec_user_image(span<const u8> bytes, char* const argv[],
                                  char* const envp[], u64* entry_out, u64* stack_out) {
        // Multithreaded exec is not supported yet: the other threads would
        // keep user contexts pointing at the unmapped old image.
        if (thread_count() > 1)
            return false;

        process staging(_pid, _parent_pid, _name);

        u64 entry = 0, stack = 0;

        if (!staging.load_user_image(bytes, &entry) ||
            !staging.build_user_stack(argv, envp, &stack))
            return false;

        release_exec_bindings(*this);

        if (!adopt_exec_bindings(*this, staging))
            return false;

        replace_image(staging);
        _signals.reset_caught_for_exec();
        reset_signal_stacks_for_exec();

        *entry_out = entry;
        *stack_out = stack;

        return true;
    }

    core::task<exec_result> process::exec_user_file_async(mm::shptr<fs::open_file> file,
                                                          usize                    length,
                                                          char* const              argv[],
                                                          char* const envp[]) {
        if (!file)
            co_return error_t::ebadf;

        // Multithreaded exec is not supported yet: the other threads would
        // keep user contexts pointing at the unmapped old image.
        if (thread_count() > 1)
            co_return error_t::einval;

        process staging(_pid, _parent_pid, _name);

        u64 entry = 0, stack = 0;

        auto loaded = co_await staging.load_user_file_async(file, length, &entry);
        if (!loaded)
            co_return loaded.error;

        if (!loaded.value || !staging.build_user_stack(argv, envp, &stack))
            co_return error_t::einval;

        release_exec_bindings(*this);
        close_on_exec_descriptors();
        // Retire the old image's tokens and start a fresh epoch: old children
        // go stale (and abort), new submissions are unaffected.
        _cancellation_root.cancel(core::cancel_reason::shutdown);
        _cancellation_root.reset();
        sys::io_exec_process(this);

        if (!adopt_exec_bindings(*this, staging))
            co_return error_t::enospc;

        replace_image(staging);
        _signals.reset_caught_for_exec();
        reset_signal_stacks_for_exec();

        co_return exec_result{entry, stack};
    }

    mm::shptr<thread> process::create_user_thread(u64 entry, char* const argv[],
                                                  char* const envp[]) {
        if (!_address_space.attached())
            return {};

        u64 user_entry_stack = user_stack_top;

        if (!build_user_stack(argv, envp, &user_entry_stack))
            return {};

        auto value = mm::share<thread>(this, proc::allocate_tid());

        if (!value || !value->prepare_user(entry, user_entry_stack, _tls_tp)) {
            unmap(user_stack_top - user_stack_bytes, user_stack_bytes);

            return {};
        }

        value->user_stack_metadata(user_stack_top - user_stack_bytes, user_stack_bytes);

        if (!add_thread(value)) {
            unmap(user_stack_top - user_stack_bytes, user_stack_bytes);

            return {};
        }

        if (!scheduler::wake(value.get())) {
            release_thread(value.get());
            unmap(user_stack_top - user_stack_bytes, user_stack_bytes);

            return {};
        }

        return value;
    }

    mm::shptr<thread> process::create_thread(u64 entry, u64 user_stack_top, u64 argument,
                                             u64 argument1, bool owns_user_stack,
                                             u64 user_stack_base, usize user_stack_size) {
        if (!_address_space.attached() || entry == 0 || user_stack_top == 0)
            return {};

        const tid_t id = proc::allocate_tid();

        u64   fs_base   = _tls_tp;
        u64   tls_base  = 0;
        usize tls_pages = 0;

        if (_tls_tp != 0 && !clone_tls_for_thread(id, fs_base, tls_base, tls_pages))
            return {};

        auto value = mm::share<thread>(this, id);

        if (!value ||
            !value->prepare_user_start(entry, user_stack_top, fs_base, argument,
                                       argument1) ||
            !add_thread(value)) {
            if (tls_base != 0 && tls_pages != 0)
                (void) unmap(tls_base, tls_pages * mm::phys_region::frame_size);

            return {};
        }

        value->tls_mapping(tls_base, tls_pages);
        value->owns_user_stack(owns_user_stack);

        if (user_stack_base != 0 && user_stack_size != 0)
            value->user_stack_metadata(user_stack_base, user_stack_size);
        else
            value->user_stack_metadata_unsupported();

        if (!scheduler::wake(value.get())) {
            release_thread(value.get());
            release_thread_tls(value.get());

            return {};
        }

        return value;
    }

    bool process::clone_tls_for_thread(tid_t id, u64& fs_base, u64& mapping_base,
                                       usize& mapping_pages) {
        if (_tls_tp == 0) {
            mapping_base  = 0;
            mapping_pages = 0;

            return true;
        }

        const u64 page_bytes = mm::phys_region::frame_size;

        mmap_region* source = nullptr;

        for (mmap_region* it = _mmap_regions; it != nullptr; it = it->next) {
            if (it->covers(_tls_tp - 1) && it->backing != nullptr) {
                source = it;

                break;
            }
        }

        if (source == nullptr)
            return false;

        u64 base = process_tls_first -
                   (static_cast<u64>(id % max_threads_per_process) * process_tls_stride);
        bool free = false;

        for (u32 attempt = 0; attempt < max_threads_per_process; ++attempt) {
            free = true;

            for (mmap_region* it = _mmap_regions; it != nullptr; it = it->next) {
                if (base < it->end() && it->base < base + source->size()) {
                    free = false;

                    break;
                }
            }

            if (free)
                break;

            base -= process_tls_stride;
        }

        if (!free)
            return false;

        // Give the thread its own lazy TLS image: only pages the process has
        // already materialised are copied, the unused reserve stays
        // demand-zero.  Sharing the region itself would require lazy page
        // references, and each thread needs a private copy of the template
        // anyway for its own errno and TLS variables.
        auto* copy = mm::phys_region::create_lazy(source->pages);
        if (copy == nullptr)
            return false;

        const usize tp_index = source->region_page +
                               static_cast<usize>((_tls_tp - source->base) / page_bytes);

        for (usize page = 0; page < source->pages; ++page) {
            const bool materialized =
                source->backing->page_base(page) != mm::frame_allocator::invalid_frame;

            if (!materialized && page != tp_index)
                continue;

            if (!copy->ensure_page(page, true)) {
                copy->release();

                return false;
            }

            if (materialized)
                __builtin_memcpy(copy->page_direct(page),
                                 source->backing->page_direct(page), page_bytes);
        }

        u64 installed = 0;

        if (!install_mapping(base, source->pages,
                             map_protection::read | map_protection::write, copy, {}, 0,
                             installed, 0, false, false, base, /*cow=*/false)) {
            copy->release();

            return false;
        }

        // The thread pointer's self-reference is per thread; its page was
        // materialised above so it cannot be a demand-zero mapping.
        fs_base = base + (_tls_tp - source->base);

        auto* tcb = static_cast<u8*>(copy->page_direct(tp_index)) +
                    ((fs_base - base) % page_bytes);

        *reinterpret_cast<u64*>(tcb) = fs_base;

        mapping_base  = base;
        mapping_pages = source->pages;

        return true;
    }

    mm::shptr<thread> process::thread_by_tid(tid_t id) {
        arch::interrupt_spinlock_guard guard(_lifecycle_lock);

        for (u32 i = 0; i < max_threads_per_process; ++i) {
            if (_threads[i] && _threads[i]->tid() == id)
                return _threads[i];
        }

        return {};
    }

    mm::shptr<thread> process::thread_stack_info(tid_t                  id,
                                                 thread_stack_snapshot& snapshot) {
        arch::interrupt_spinlock_guard guard(_lifecycle_lock);

        for (u32 i = 0; i < max_threads_per_process; ++i) {
            if (!_threads[i] || _threads[i]->tid() != id ||
                _threads[i]->state() == thread_state::dead)
                continue;

            snapshot.stack_base = _threads[i]->user_stack_base();
            snapshot.stack_size = _threads[i]->user_stack_size();
            snapshot.detached   = _threads[i]->detached();
            snapshot.supported  = _threads[i]->user_stack_metadata_supported();

            return _threads[i];
        }

        return {};
    }

    bool process::copy_descriptors_from(const process& other) {
        for (u32 fd = 0; fd < max_process_fds; ++fd) {
            auto entry = other._descriptors.get(fd_t{static_cast<i32>(fd)});
            auto copy =
                entry ? mm::share<fs::open_file>(*entry) : mm::shptr<fs::open_file>{};

            if (entry && (!copy || !_descriptors.set(fd_t{static_cast<i32>(fd)}, copy,
                                                     other._descriptors.flags(
                                                         fd_t{static_cast<i32>(fd)}))))
                return false;
        }

        return true;
    }

    mm::shptr<thread> process::create_fork_thread(const arch::user_context& uctx,
                                                  signal_mask blocked_signals,
                                                  u64         user_stack_base,
                                                  usize       user_stack_size) {
        auto value = mm::share<thread>(this, proc::allocate_tid());

        if (!value || !value->prepare_fork_user(uctx))
            return {};

        if (user_stack_base != 0 && user_stack_size != 0)
            value->user_stack_metadata(user_stack_base, user_stack_size);
        else
            value->user_stack_metadata_unsupported();

        value->signals().set_blocked(blocked_signals);

        if (!add_thread(value))
            return {};

        if (!scheduler::wake(value.get())) {
            release_thread(value.get());

            return {};
        }

        return value;
    }
} // namespace astia::proc
