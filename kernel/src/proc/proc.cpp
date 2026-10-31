#include "proc.hpp"

#include "../sys/user_mem.hpp"
#include "scheduler.hpp"
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/api/termios.h>
#include <astia/arch/cpu.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/log.hpp>
#include <astia/mm/heap.hpp>
#include <astia/mm/share.hpp>

void* operator new[](astia::usize size, void* where) noexcept;

namespace astia::proc {

    namespace {
        /// This module's log source, and the image-loading half of it.
        using logs       = astia::log::context<"proc">;
        using spawn_logs = logs::child<"spawn">;

        process*             kernel   = nullptr;
        thread*              current[scheduler::max_cpu_count]{};
        tid_t                next_tid = 1;
        pid_t                next_pid{1};

        // Non-trivial element type in freestanding code: static storage
        // plus placement construction, so the global itself stays
        // trivially destructible (same pattern as fs::devfs_instance).
        alignas(mm::shptr<process>) u8
            registry_storage[sizeof(mm::shptr<process>) * max_processes];
        mm::shptr<process>* registry = nullptr;
        core::spinlock      registry_lock;

        /// registry_lock must be held.
        i32 find_slot_locked(pid_t id) {
            for (u32 i = 0; i < max_processes; ++i) {
                if (registry[i] && registry[i]->pid() == id) {
                    return static_cast<i32>(i);
                }
            }
            return -1;
        }

        bool is_live(const process* value) {
            return value != nullptr && value->state() != process_state::zombie &&
                   value->state() != process_state::dead;
        }

        // registry_lock must be held.  Looking up the requested PID alone
        // would incorrectly allow setpgid() to join an arbitrary process
        // rather than an existing process group.
        process* find_group_locked(pgrp_t group, pid_t session) {
            for (u32 i = 0; i < max_processes; ++i) {
                process* value = registry[i].get();
                if (is_live(value) && value->process_group() == group &&
                    value->session() == session) {
                    return value;
                }
            }
            return nullptr;
        }

        pgrp_t process_group_for(pid_t id) {
            return pgrp_t{static_cast<u32>(id)};
        }

        bool is_process_group_leader(const process* value) {
            return value != nullptr && value->process_group() == process_group_for(value->pid());
        }
    }

    bool register_process(const mm::shptr<process>& value) {
        core::spinlock_guard guard(registry_lock);
        for (u32 i = 1; i < max_processes; ++i) {
            if (!registry[i]) {
                registry[i] = value;
                return true;
            }
        }
        return false;
    }

    bool unregister_process(process* value) {
        if (value == nullptr)
            return false;
        core::spinlock_guard guard(registry_lock);
        for (u32 i = 1; i < max_processes; ++i) {
            if (registry[i].get() == value) {
                registry[i] = {};
                return true;
            }
        }
        return false;
    }

    pid_t allocate_pid() {
        const pid_t value = next_pid;
        next_pid          = pid_t{static_cast<u32>(static_cast<u32>(next_pid) + 1)};
        return value;
    }

    bool init() {
        if (kernel != nullptr) {
            return false;
        }

        registry    = ::new (registry_storage) mm::shptr<process>[max_processes];
        registry[0] = mm::share<process>(pid_t{}, pid_t{}, "kernel");
        kernel      = registry[0].get();
        if (kernel == nullptr || !kernel->install_standard_streams()) {
            kernel      = nullptr;
            registry[0] = {};
            return false;
        }

        auto bootstrap = mm::share<thread>(kernel, next_tid++);
        if (!bootstrap || !kernel->add_thread(bootstrap)) {
            kernel      = nullptr;
            registry[0] = {};
            return false;
        }
        bootstrap->state(thread_state::running);
        current[arch::cpu::current_index()] = bootstrap.get();
        kernel->state(process_state::runnable);
        return true;
    }

    process* kernel_process() {
        return kernel;
    }

    process* current_process() {
        thread* value = current_thread();
        return value == nullptr ? nullptr : value->owner();
    }

    thread* current_thread() {
        const u32 index = arch::cpu::current_index();
        return index < scheduler::max_cpu_count ? current[index] : nullptr;
    }

    tid_t allocate_tid() {
        return next_tid++;
    }

    void set_current_thread(thread* value) {
        const u32 index = arch::cpu::current_index();
        if (index < scheduler::max_cpu_count)
            current[index] = value;
    }

    optional<mm::shptr<process>> spawnve(const char* path, char* const argv[],
                                         char* const envp[]) {
        const auto fail = [](error_t error) -> optional<mm::shptr<process>> {
            return optional<mm::shptr<process>>(error);
        };
        process* parent = current_process();
        if (parent == nullptr || path == nullptr) {
            return fail(error_t::einval);
        }
        const fs::file_mode image_mode = fs::node_mode(path);
        if (image_mode.exists() &&
            !parent->can_access(image_mode, fs::node_uid(path), fs::node_gid(path),
                                fs::permission_bits::execute))
            return fail(error_t::eacces);
        auto image_file = fs::open_path(path);
        if (!image_file) {
            spawn_logs::error("open failed for %s", path);
            return fail(error_t::enoent);
        }
        const long image_size = image_file->size();
        if (image_size <= 0) {
            spawn_logs::error("invalid image size=%ld", image_size);
            return fail(error_t::einval);
        }
        auto image = mm::heap::alloc(static_cast<usize>(image_size));
        if (!image) {
            spawn_logs::error("image allocation failed size=%ld", image_size);
            return fail(error_t::enomem);
        }
        const long loaded =
            image_file->read(image.value, static_cast<u32>(image_size), 0);
        if (loaded != image_size) {
            spawn_logs::error("short read=%ld expected=%ld", loaded, image_size);
            mm::heap::free(image.value);
            return fail(error_t::eio);
        }

        const pid_t child_id = allocate_pid();
        auto        child    = mm::share<process>(child_id, parent->pid(), path);
        if (!child) {
            mm::heap::free(image.value);
            return fail(error_t::enomem);
        }
        child->inherit_groups(*parent);
        // The thread is queued before registration.  Publish the runnable
        // process state first so a fast child exit cannot be overwritten as
        // runnable after it has become a zombie.
        child->state(process_state::runnable);
        // exec-style spawn inherits redirected descriptors.  Replacing them
        // with fresh console streams would make a child read forever from the
        // terminal after the parent dup2'ed a file onto stdin.
        if (!child->copy_descriptors_from(*parent)) {
            mm::heap::free(image.value);
            return fail(error_t::enomem);
        }
        child->close_on_exec_descriptors();
        u64        entry = 0;
        const bool loaded_image =
            child->load_user_image(image.value, static_cast<usize>(image_size), &entry);
        mm::heap::free(image.value);
        if (!loaded_image) {
            spawn_logs::error("ELF load or user thread setup failed");
            return fail(error_t::einval);
        }
        if (!register_process(child)) {
            spawn_logs::error("process registry full");
            return fail(error_t::eagain);
        }
        if (!child->create_user_thread(entry, argv, envp)) {
            (void) unregister_process(child.get());
            parent->wake_waiters();
            spawn_logs::error("user thread setup failed");
            return fail(error_t::eagain);
        }
        return optional<mm::shptr<process>>(child);
    }

    process* find_process(pid_t id) {
        core::spinlock_guard guard(registry_lock);
        const i32            slot = find_slot_locked(id);
        return slot < 0 ? nullptr : registry[slot].get();
    }

    i64 process_snapshot(u64 output, u32 capacity) {
        if (capacity > ASTIA_PROCESS_SNAPSHOT_MAX || (capacity != 0 && output == 0))
            return -EINVAL;
        astia_process_info records[ASTIA_PROCESS_SNAPSHOT_MAX]{};
        u32                count = 0;
        {
            core::spinlock_guard guard(registry_lock);
            for (u32 i = 0; i < max_processes && count < capacity; ++i) {
                process* value = registry[i].get();
                if (value == nullptr)
                    continue;
                auto& record = records[count++];
                record.pid   = static_cast<i32>(raw_id(value->pid()));
                record.ppid  = static_cast<i32>(raw_id(value->parent_pid()));
                record.pgid  = static_cast<i32>(raw_id(value->process_group()));
                record.uid   = value->uid();
                record.gid   = value->gid();
                record.state = static_cast<u32>(value->state());
                record.has_ctty  = value->controlling_terminal() != nullptr ? 1u : 0u;
                const char* name = value->name();
                for (u32 n = 0; name != nullptr && n + 1 < sizeof(record.name) && name[n];
                     ++n)
                    record.name[n] = name[n];
            }
        }
        if (count != 0 &&
            !sys::copy_out(sys::user_range::bytes(output, count * sizeof(records[0])),
                           records))
            return -EFAULT;
        return count;
    }

    pgrp_t getpgrp() {
        process* owner = current_process();
        return owner == nullptr ? pgrp_t{} : owner->process_group();
    }

    i64 getsid(pid_t target) {
        process* owner = current_process();
        if (owner == nullptr)
            return -ESRCH;
        if (target == pid_t{})
            return static_cast<i64>(raw_id(owner->session()));

        // Hold the registry reference while reading the selected process.
        // Zombies remain registered until their parent reaps them, matching
        // native process lifetime; reaping removes the PID atomically.
        core::spinlock_guard guard(registry_lock);
        const i32            slot = find_slot_locked(target);
        if (slot < 0 || !registry[slot])
            return -ESRCH;
        return static_cast<i64>(raw_id(registry[slot]->session()));
    }

    i64 acquire_controlling_terminal(fs::file* terminal) {
        process* owner = current_process();
        if (owner == nullptr || terminal == nullptr)
            return -ENXIO;
        if (owner->pid() != owner->session() || owner->controlling_terminal() != nullptr)
            return -EPERM;
        owner->controlling_terminal(terminal);
        i32        group = static_cast<i32>(raw_id(owner->process_group()));
        const long rc    = fs::dispatch_ioctl(terminal, TIOCSPGRP, &group, sizeof(i32));
        if (rc < 0) {
            owner->controlling_terminal(nullptr);
            return rc;
        }
        return 0;
    }

    fs::file* controlling_terminal() {
        process* owner = current_process();
        return owner == nullptr ? nullptr : owner->controlling_terminal();
    }

    bool has_controlling_terminal() {
        return controlling_terminal() != nullptr;
    }

    bool process_group_exists(pgrp_t group, pid_t session) {
        core::spinlock_guard guard(registry_lock);
        return find_group_locked(group, session) != nullptr;
    }

    i64 terminal_signal_foreground(pgrp_t foreground, signal_number signal) {
        if (foreground == pgrp_t{})
            return -ESRCH;
        return signal_process_group(foreground, signal);
    }

    bool terminal_read_allowed(pgrp_t foreground) {
        process* owner = current_process();
        if (owner == nullptr || foreground == pgrp_t{} ||
            owner->process_group() == foreground)
            return true;
        // A real terminal stops the background group.  The scheduler does not
        // yet expose stopped-child state to waitpid, so report the POSIX
        // fallback error after queuing SIGTTIN rather than allowing a read
        // from the wrong job.
        (void) signal_process_group(owner->process_group(), signal_number::ttin);
        return false;
    }

    bool terminal_write_allowed(pgrp_t foreground, bool tostop) {
        process* owner = current_process();
        if (owner == nullptr || foreground == pgrp_t{} ||
            owner->process_group() == foreground || !tostop)
            return true;
        (void) signal_process_group(owner->process_group(), signal_number::ttou);
        return false;
    }

    bool terminal_set_foreground_allowed(pgrp_t foreground) {
        process* owner = current_process();
        if (owner == nullptr)
            return false;
        return process_group_exists(foreground, owner->session());
    }

    i64 setpgid(pid_t pid, pgrp_t pgid) {
        process* caller = current_process();
        if (caller == nullptr)
            return -ESRCH;
        const bool self_target = pid == pid_t{};
        if (self_target)
            pid = caller->pid();
        if (pgid == pgrp_t{})
            pgid = pgrp_t{raw_id(pid)};
        if (pid == pid_t{} || pgid == pgrp_t{})
            return -EINVAL;

        core::spinlock_guard guard(registry_lock);
        const i32            slot = find_slot_locked(pid);
        // A freshly forked child can execute setpgid(0, ...) before another
        // registry scan observes its slot.  pid==0 is explicitly the caller,
        // so use that stable object directly.
        process* target =
            self_target ? caller : (slot < 0 ? nullptr : registry[slot].get());
        if (!is_live(target)) {
            return -ESRCH;
        }
        if (target != caller && target->parent_pid() != caller->pid()) {
            return -ESRCH;
        }
        if (target->session() != caller->session()) {
            return -EPERM;
        }
        // POSIX rejects setpgid() for a session leader, including a
        // redundant request for its current process group.
        if (is_process_group_leader(target) && target->session() == target->pid()) {
            return -EPERM;
        }

        if (pgid != pgrp_t{raw_id(target->pid())}) {
            if (find_group_locked(pgid, caller->session()) == nullptr) {
                return -EPERM;
            }
        }
        target->process_group(pgid);
        return 0;
    }

    i64 setsid() {
        process* owner = current_process();
        if (owner == nullptr)
            return -ESRCH;
        if (is_process_group_leader(owner))
            return -EPERM;
        owner->session(owner->pid());
        owner->process_group(process_group_for(owner->pid()));
        owner->controlling_terminal(nullptr);
        return static_cast<i64>(raw_id(owner->session()));
    }

    i64 signal_process_group(pgrp_t group, signal_number value) {
        if (!valid_signal(value) && value != signal_number{})
            return -EINVAL;
        bool     found = false;
        process* targets[max_processes]{};
        u32      count = 0;
        {
            core::spinlock_guard guard(registry_lock);
            for (u32 i = 0; i < max_processes; ++i) {
                process* target = registry[i].get();
                if (!is_live(target) || target->process_group() != group)
                    continue;
                found            = true;
                targets[count++] = target;
            }
        }
        if (value != signal_number{}) {
            for (u32 i = 0; i < count; ++i) {
                if (!targets[i]->signals().send(value))
                    return -EINVAL;
                if (is_uncatchable(value) ||
                    targets[i]->signals().disposition(value).action !=
                        signal_action::ignore)
                    targets[i]->signal_state_change(value);
                targets[i]->wake_signal_waiters();
            }
        }
        return found ? 0 : -ESRCH;
    }

    i64 waitpid(i32 pid, i32* status, wait_options options) {
        process* parent = current_process();
        thread*  self   = current_thread();
        if (parent == nullptr || self == nullptr) {
            return -ESRCH;
        }
        const bool   group_wait = pid < -1;
        const pgrp_t requested_group =
            group_wait ? pgrp_t{static_cast<u32>(-pid)}
                       : (pid == 0 ? parent->process_group() : pgrp_t{});
        if (static_cast<u32>(options) & ~valid_wait_option_bits)
            return -EINVAL;

        // From here to the blocking yield the scan and the waiter
        // registration must be atomic against child exits, which can
        // only happen by running the child on this single CPU.
        arch::cpu::nointerrupts();
        i64 result = 0;
        for (;;) {
            if (!parent->add_waiter(self)) {
                // Never block without a slot: child exit would have no way
                // to wake this thread, leaving waitpid stuck forever.
                result = -EAGAIN;
                break;
            }

            process*           zombie_child = nullptr;
            bool               has_children = false;
            mm::shptr<process> taken;
            {
                core::spinlock_guard guard(registry_lock);
                for (u32 i = 0; i < max_processes; ++i) {
                    process* child = registry[i].get();
                    if (child == nullptr || child->parent_pid() != parent->pid()) {
                        continue;
                    }
                    if (pid > 0 && child->pid() != pid_t{static_cast<u32>(pid)}) {
                        continue;
                    }
                    if ((group_wait || pid == 0) &&
                        child->process_group() != requested_group) {
                        continue;
                    }
                    has_children = true;
                    if (child->state() == process_state::zombie) {
                        taken        = registry[i];
                        registry[i]  = {};
                        zombie_child = child;
                        break;
                    }
                    if ((static_cast<u32>(options) &
                         static_cast<u32>(wait_options::untraced)) &&
                        child->stop_event_pending()) {
                        if (status != nullptr)
                            *status = child->stop_status();
                        child->clear_stop_event();
                        result = static_cast<i64>(raw_id(child->pid()));
                        break;
                    }
                    if ((static_cast<u32>(options) &
                         static_cast<u32>(wait_options::continued)) &&
                        child->continued_event_pending()) {
                        if (status != nullptr)
                            *status = astia::wait_continued_marker;
                        child->clear_continued_event();
                        result = static_cast<i64>(raw_id(child->pid()));
                        break;
                    }
                }
            }

            if (zombie_child != nullptr) {
                // `taken` now owns the registry's reference; dropping it
                // below runs ~process, which frees the address space
                // and the dead threads' kernel stacks.  The child was
                // already switched off this CPU before we were resumed.
                const pid_t child_pid = zombie_child->pid();
                if (status != nullptr) {
                    *status = zombie_child->wait_status();
                }
                result = static_cast<i64>(raw_id(child_pid));
                parent->remove_waiter(self);
                taken = {};
                break;
            }
            if (result != 0) {
                parent->remove_waiter(self);
                break;
            }
            if (!has_children) {
                parent->remove_waiter(self);
                result = -ECHILD;
                break;
            }
            if (static_cast<u32>(options) & static_cast<u32>(wait_options::nohang)) {
                parent->remove_waiter(self);
                result = 0;
                break;
            }

            // A live child exists: block until thread_exited() wakes us.
            // The wake clears our waiter slot, so it is re-added at the
            // top of the next scan.
            self->state(thread_state::blocked);
            scheduler::yield();
        }
        arch::cpu::interrupts();
        return result;
    }

} // namespace astia::proc
