#include "proc.hpp"

#include "../sys/user_mem.hpp"
#include "scheduler.hpp"
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/api/termios.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
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

        process* kernel = nullptr;
        thread*  current[scheduler::max_cpu_count]{};
        // These counters are deliberately raw: the public IDs remain domain
        // types, while the atomic operation can advance the underlying ABI
        // value without taking the process registry lock.  Unsigned fetch-add
        // wraps exactly as the old increment did.
        u32 next_tid = 1;
        u32 next_pid = 1;

        // Non-trivial element type in freestanding code: static storage
        // plus placement construction, so the global itself stays
        // trivially destructible (same pattern as fs::devfs_instance).
        alignas(mm::shptr<process>) u8
            registry_storage[sizeof(mm::shptr<process>) * max_processes];
        mm::shptr<process>* registry = nullptr;
        core::spinlock      registry_lock;

        bool is_live(const process* value) {
            return value != nullptr && value->state() != process_state::zombie &&
                   value->state() != process_state::dead;
        }

        u32 snapshot_registry(mm::shptr<process>* values) {
            u32                            count = 0;
            arch::interrupt_spinlock_guard guard(registry_lock);
            for (u32 i = 0; i < max_processes; ++i) {
                if (registry[i])
                    values[count++] = registry[i];
            }
            return count;
        }

        bool take_registered(process* value, mm::shptr<process>& taken) {
            if (value == nullptr)
                return false;
            arch::interrupt_spinlock_guard guard(registry_lock);
            for (u32 i = 1; i < max_processes; ++i) {
                if (registry[i].get() == value) {
                    taken = static_cast<mm::shptr<process>&&>(registry[i]);
                    return true;
                }
            }
            return false;
        }

        pgrp_t process_group_for(pid_t id) {
            return pgrp_from_raw(raw_id(id));
        }

        bool is_process_group_leader(const process* value) {
            return value != nullptr &&
                   value->process_group() == process_group_for(value->pid());
        }
    }

    bool register_process(const mm::shptr<process>& value) {
        arch::interrupt_spinlock_guard guard(registry_lock);
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
        mm::shptr<process> removed;
        {
            arch::interrupt_spinlock_guard guard(registry_lock);
            for (u32 i = 1; i < max_processes; ++i) {
                if (registry[i].get() == value) {
                    removed = static_cast<mm::shptr<process>&&>(registry[i]);
                    break;
                }
            }
        }
        return static_cast<bool>(removed);
    }

    pid_t allocate_pid() {
        return pid_from_raw(__atomic_fetch_add(&next_pid, 1u, __ATOMIC_RELAXED));
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

        auto bootstrap = mm::share<thread>(kernel, allocate_tid());
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
        return __atomic_fetch_add(&next_tid, 1u, __ATOMIC_RELAXED);
    }

    void set_current_thread(thread* value) {
        const u32 index = arch::cpu::current_index();
        if (index < scheduler::max_cpu_count)
            current[index] = value;
    }

    void expire_alarms() {
        const u64 now = arch::timer_ticks();
        mm::shptr<process> values[max_processes];
        const u32 count = snapshot_registry(values);
        for (u32 i = 0; i < count; ++i) {
            if (is_live(values[i].get()))
                values[i]->expire_alarm(now);
        }
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

    core::task<mm::shptr<process>> spawnve_async(
        process* parent, const mm::shptr<fs::open_file>& file, usize length,
        char* const argv[], char* const envp[]) {
        if (parent == nullptr || !file || length == 0)
            co_return error_t::einval;

        auto child = mm::share<process>(allocate_pid(), parent->pid(), file->path());
        if (!child)
            co_return error_t::enomem;
        child->inherit_groups(*parent);
        child->state(process_state::runnable);
        if (!child->copy_descriptors_from(*parent))
            co_return error_t::enomem;
        child->close_on_exec_descriptors();

        u64 entry = 0;
        auto loaded = co_await child->load_user_file_async(file, length, &entry);
        if (!loaded || !loaded.value)
            co_return loaded.error == error_t::enone ? error_t::einval : loaded.error;

        if (!register_process(child))
            co_return error_t::eagain;
        if (!child->create_user_thread(entry, argv, envp)) {
            (void) unregister_process(child.get());
            parent->wake_waiters();
            co_return error_t::eagain;
        }
        co_return static_cast<mm::shptr<process>&&>(child);
    }

    mm::shptr<process> find_process(pid_t id) {
        mm::shptr<process> values[max_processes];
        const u32          count = snapshot_registry(values);
        for (u32 i = 0; i < count; ++i) {
            if (values[i]->pid() == id)
                return values[i];
        }
        return {};
    }

    mm::shptr<process> retain_process(pid_t id) {
        mm::shptr<process> values[max_processes];
        const u32          count = snapshot_registry(values);
        for (u32 i = 0; i < count; ++i) {
            if (values[i]->pid() == id)
                // snapshot_registry already retained the registry entry.
                // Avoid a second registry-lock pass and return that reference.
                return static_cast<mm::shptr<process>&&>(values[i]);
        }
        return {};
    }

    i64 process_snapshot(u64 output, u32 capacity) {
        if (capacity > ASTIA_PROCESS_SNAPSHOT_MAX || (capacity != 0 && output == 0))
            return -EINVAL;
        astia_process_info records[ASTIA_PROCESS_SNAPSHOT_MAX]{};
        mm::shptr<process> values[max_processes];
        const u32          process_count = snapshot_registry(values);
        u32                count         = 0;
        for (u32 i = 0; i < process_count && count < capacity; ++i) {
            process* value  = values[i].get();
            auto&    record = records[count++];
            record.pid      = static_cast<i32>(raw_id(value->pid()));
            record.ppid     = static_cast<i32>(raw_id(value->parent_pid()));
            record.state    = static_cast<u32>(value->state());
            process::metadata_snapshot metadata{};
            value->snapshot_metadata(metadata);
            record.pgid      = static_cast<i32>(raw_id(metadata.process_group));
            record.uid       = metadata.uid;
            record.gid       = metadata.gid;
            record.has_ctty  = metadata.controlling_terminal ? 1u : 0u;
            const char* name = value->name();
            for (u32 n = 0; name != nullptr && n + 1 < sizeof(record.name) && name[n];
                 ++n)
                record.name[n] = name[n];
        }
        const auto destination =
            sys::user_ptr<astia_process_info>(output).validate(count);
        if (count != 0 &&
            (!destination || !sys::copy_out(destination.value.bytes(count), records)))
            return -EFAULT;
        return count;
    }

    pgrp_t getpgrp() {
        process* owner = current_process();
        return owner == nullptr ? pgrp_t{} : owner->process_group();
    }

    i64 getpgid(pid_t target) {
        process* owner = current_process();
        if (owner == nullptr)
            return -ESRCH;
        if (target == pid_t{})
            return static_cast<i64>(raw_id(owner->process_group()));

        mm::shptr<process> selected = retain_process(target);
        if (!selected)
            return -ESRCH;
        return static_cast<i64>(raw_id(selected->process_group()));
    }

    i64 getsid(pid_t target) {
        process* owner = current_process();
        if (owner == nullptr)
            return -ESRCH;
        if (target == pid_t{})
            return static_cast<i64>(raw_id(owner->session()));

        mm::shptr<process> selected = retain_process(target);
        if (!selected)
            return -ESRCH;
        return static_cast<i64>(raw_id(selected->session()));
    }

    i64 acquire_controlling_terminal(const mm::shptr<fs::open_file>& terminal) {
        process* owner = current_process();
        if (owner == nullptr || !terminal)
            return -ENXIO;
        if (!owner->claim_controlling_terminal(terminal))
            return -EPERM;
        i32        group = static_cast<i32>(raw_id(owner->process_group()));
        const long rc    = terminal->ioctl(TIOCSPGRP, &group, sizeof(i32));
        if (rc < 0) {
            owner->controlling_terminal({});
            return rc;
        }
        return 0;
    }

    mm::shptr<fs::open_file> controlling_terminal() {
        process* owner = current_process();
        return owner == nullptr ? mm::shptr<fs::open_file>{}
                                : owner->controlling_terminal();
    }

    bool has_controlling_terminal() {
        return static_cast<bool>(controlling_terminal());
    }

    bool process_group_exists(pgrp_t group, pid_t session) {
        mm::shptr<process> values[max_processes];
        const u32          count = snapshot_registry(values);
        for (u32 i = 0; i < count; ++i) {
            process* value = values[i].get();
            if (is_live(value) && value->process_group() == group &&
                value->session() == session)
                return true;
        }
        return false;
    }

    i64 terminal_signal_foreground(pgrp_t foreground, signal_number signal) {
        if (foreground == pgrp_t{})
            return -ESRCH;
        return signal_process_group(foreground, signal);
    }

    namespace {
        terminal_access_result terminal_background_access(process* owner,
                                                          pgrp_t foreground,
                                                          signal_number signal) {
            if (owner == nullptr || foreground == pgrp_t{} ||
                owner->process_group() == foreground)
                return terminal_access_result::allowed;

            const signal_mask bit = signal_bit(signal);
            auto* self = current_thread();
            if (self == nullptr || (self->signals().blocked() & bit) != 0 ||
                owner->signals().disposition(signal).action == signal_action::ignore)
                return terminal_access_result::io_error;
            if (signal_process_group(owner->process_group(), signal) < 0)
                return terminal_access_result::io_error;
            return owner->signals().disposition(signal).action == signal_action::handler
                       ? terminal_access_result::interrupted
                       : terminal_access_result::retry;
        }
    }

    terminal_access_result terminal_read_allowed(pgrp_t foreground) {
        process* owner = current_process();
        return terminal_background_access(owner, foreground, signal_number::ttin);
    }

    terminal_access_result terminal_write_allowed(pgrp_t foreground, bool tostop) {
        process* owner = current_process();
        if (owner == nullptr || foreground == pgrp_t{} ||
            owner->process_group() == foreground || !tostop)
            return terminal_access_result::allowed;
        return terminal_background_access(owner, foreground, signal_number::ttou);
    }

    terminal_access_result terminal_set_foreground_allowed(pgrp_t foreground,
                                                           pgrp_t requested) {
        process* owner = current_process();
        if (owner == nullptr)
            return terminal_access_result::denied;
        if (!process_group_exists(requested, owner->session()))
            return terminal_access_result::denied;
        return terminal_background_access(owner, foreground, signal_number::ttou);
    }

    i64 setpgid(pid_t pid, pgrp_t pgid) {
        process* caller = current_process();
        if (caller == nullptr)
            return -ESRCH;
        const bool self_target = pid == pid_t{};
        if (self_target)
            pid = caller->pid();
        if (pgid == pgrp_t{})
            pgid = pgrp_from_raw(raw_id(pid));
        if (pid == pid_t{} || pgid == pgrp_t{})
            return -EINVAL;

        mm::shptr<process> values[max_processes];
        const u32          value_count = snapshot_registry(values);
        process*           target      = self_target ? caller : nullptr;
        for (u32 i = 0; i < value_count && target == nullptr; ++i) {
            if (values[i]->pid() == pid)
                target = values[i].get();
        }
        // A freshly forked child can execute setpgid(0, ...) before another
        // registry scan observes its slot.  pid==0 is explicitly the caller,
        // so use that stable object directly.
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

        if (pgid != pgrp_from_raw(raw_id(target->pid()))) {
            bool group_exists = false;
            for (u32 i = 0; i < value_count; ++i) {
                process* value = values[i].get();
                if (is_live(value) && value->process_group() == pgid &&
                    value->session() == caller->session()) {
                    group_exists = true;
                    break;
                }
            }
            if (!group_exists) {
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
        owner->controlling_terminal({});
        return static_cast<i64>(raw_id(owner->session()));
    }

    i64 signal_process_group(pgrp_t group, signal_number value) {
        if (!valid_signal(value) && value != signal_number{})
            return -EINVAL;
        bool               found = false;
        mm::shptr<process> targets[max_processes];
        const u32          value_count = snapshot_registry(targets);
        u32                count       = 0;
        for (u32 i = 0; i < value_count; ++i) {
            process* target = targets[i].get();
            if (!is_live(target) || target->process_group() != group)
                continue;
            found            = true;
            targets[count++] = static_cast<mm::shptr<process>&&>(targets[i]);
        }
        if (value != signal_number{}) {
            for (u32 i = 0; i < count; ++i) {
                if (!targets[i]->signals().send(value))
                    return -EINVAL;
                if (is_uncatchable(value) ||
                    targets[i]->signals().disposition(value).action ==
                        signal_action::default_action)
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
            group_wait ? wait_group_from_selector(pid)
                       : (pid == 0 ? parent->process_group() : pgrp_t{});
        if (raw_wait_options(options) & ~valid_wait_option_bits)
            return -EINVAL;

        // From here to the blocking yield the scan and the waiter
        // registration must be atomic against child exits, which can
        // only happen by running the child on this single CPU.
        arch::cpu::nointerrupts();
        i64 result = 0;
        for (;;) {
            self->state(thread_state::blocked);
            if (!parent->add_waiter(self)) {
                // Never block without a slot: child exit would have no way
                // to wake this thread, leaving waitpid stuck forever.
                self->state(thread_state::running);
                result = -EAGAIN;
                break;
            }
            bool               has_children = false;
            mm::shptr<process> taken;
            mm::shptr<process> children[max_processes];
            const u32          child_count  = snapshot_registry(children);
            process*           zombie_child = nullptr;
            for (u32 i = 0; i < child_count; ++i) {
                process* child = children[i].get();
                if (child == nullptr || child->parent_pid() != parent->pid())
                    continue;
                if (pid > 0 && child->pid() != wait_pid_from_selector(pid))
                    continue;
                if ((group_wait || pid == 0) && child->process_group() != requested_group)
                    continue;
                has_children = true;
                if (child->state() == process_state::zombie) {
                    zombie_child = child;
                    break;
                }
                if (has_wait_option(options, wait_options::untraced) &&
                    child->stop_event_pending()) {
                    if (status != nullptr)
                        *status = child->stop_status();
                    child->clear_stop_event();
                    result = static_cast<i64>(raw_id(child->pid()));
                    break;
                }
                if (has_wait_option(options, wait_options::continued) &&
                    child->continued_event_pending()) {
                    if (status != nullptr)
                        *status = astia::wait_continued_marker;
                    child->clear_continued_event();
                    result = static_cast<i64>(raw_id(child->pid()));
                    break;
                }
            }

            if (zombie_child != nullptr) {
                // Membership is removed under the registry lock, but the
                // reference is moved out so a final release cannot destroy a
                // process while that lock is held.
                if (!take_registered(zombie_child, taken)) {
                    self->state(thread_state::running);
                    continue;
                }
                // `taken` now owns the registry's reference; dropping it
                // below runs ~process, which frees the address space
                // and the dead threads' kernel stacks.  The child was
                // already switched off this CPU before we were resumed.
                const pid_t child_pid = taken->pid();
                if (status != nullptr) {
                    *status = taken->wait_status();
                }
                result = static_cast<i64>(raw_id(child_pid));
                parent->remove_waiter(self);
                self->state(thread_state::running);
                taken = {};
                break;
            }
            if (result != 0) {
                parent->remove_waiter(self);
                self->state(thread_state::running);
                break;
            }
            if (!has_children) {
                parent->remove_waiter(self);
                self->state(thread_state::running);
                result = -ECHILD;
                break;
            }
            if (has_wait_option(options, wait_options::nohang)) {
                parent->remove_waiter(self);
                self->state(thread_state::running);
                result = 0;
                break;
            }

            if (parent->signals().has_unblocked_interrupting_for(self->signals().blocked())) {
                parent->remove_waiter(self);
                self->state(thread_state::running);
                result = -EINTR;
                break;
            }

            // A live child exists: block until thread_exited() wakes us.
            // The wake clears our waiter slot, so it is re-added at the
            // top of the next scan.
            scheduler::yield();
        }
        arch::cpu::interrupts();
        return result;
    }

} // namespace astia::proc
