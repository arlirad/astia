#include "proc.hpp"

#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/api/termios.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/arch/platform.hpp>
#include <astia/arch/spinlock_guard.hpp>
#include <astia/atomic.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/mm/share.hpp>

#include "../sys/aio.hpp"
#include "../sys/user_mem.hpp"
#include "events.hpp"
#include "scheduler.hpp"
#include "signal_permission.hpp"
#include "terminal_name.hpp"

void* operator new[](astia::usize size, void* where) noexcept;

namespace astia::proc {
    namespace {
        process* kernel = nullptr;
        thread*  current[arch::platform::max_cpu_count]{};
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
            u32 count = 0;

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
        return pid_from_raw(
            astia::atomic_ref<u32>(next_pid).fetch_add(1u, astia::memory_order::relaxed));
    }

    bool init() {
        if (kernel != nullptr)
            return false;

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

        return index < arch::platform::max_cpu_count ? current[index] : nullptr;
    }

    void terminate_current_if_killed() {
        auto* self = current_thread();

        if (self == nullptr || self->owner() == nullptr)
            return;

        auto* owner = self->owner();

        if (self->kill_pending()) {
            const signal_number signal = owner->termination_signal() == signal_number{}
                                             ? signal_number::kill
                                             : owner->termination_signal();

            owner->signal_termination(signal);
            self->exit(reinterpret_cast<void*>(
                static_cast<usize>(wait_signal_exit_status(static_cast<u8>(signal)))));
            __builtin_unreachable();
        }

        if (owner->process_exit_requested() && self->exit_requested()) {
            self->exit(reinterpret_cast<void*>(
                static_cast<usize>(owner->requested_exit_status())));
            __builtin_unreachable();
        }

        signal_number signal =
            self->signals().pending_fatal_default_for(self->signals().blocked());

        if (!valid_signal(signal))
            signal =
                owner->signals().pending_fatal_default_for(self->signals().blocked());

        if (!valid_signal(signal))
            return;

        owner->terminate_for_signal(signal, self);
        self->exit(reinterpret_cast<void*>(
            static_cast<usize>(wait_signal_exit_status(static_cast<u8>(signal)))));
        __builtin_unreachable();
    }

    tid_t allocate_tid() {
        return astia::atomic_ref<u32>(next_tid).fetch_add(1u,
                                                          astia::memory_order::relaxed);
    }

    void set_current_thread(thread* value) {
        const u32 index = arch::cpu::current_index();

        if (index < arch::platform::max_cpu_count)
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

    void expire_signal_waiters() {
        const u64 hz = arch::timer_hz();
        if (hz == 0)
            return;

        const u64 now          = arch::timer_ticks();
        const u64 monotonic_ns = (now / hz) * nanoseconds_per_second +
                                 (now % hz) * nanoseconds_per_second / hz;

        mm::shptr<process> values[max_processes];

        const u32 count = snapshot_registry(values);

        for (u32 i = 0; i < count; ++i) {
            if (is_live(values[i].get()))
                values[i]->expire_signal_waiters(monotonic_ns);
        }
    }

    core::task<mm::shptr<process>> spawnve(process* parent, mm::shptr<fs::open_file> file,
                                           usize length, char* const argv[],
                                           char* const envp[]) {
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

        const u32 count = snapshot_registry(values);

        for (u32 i = 0; i < count; ++i) {
            if (values[i]->pid() == id)
                return values[i];
        }

        return {};
    }

    mm::shptr<process> retain_process(pid_t id) {
        mm::shptr<process> values[max_processes];

        const u32 count = snapshot_registry(values);

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

        const u32 process_count = snapshot_registry(values);
        u32       count         = 0;

        for (u32 i = 0; i < process_count && count < capacity; ++i) {
            process* value = values[i].get();

            auto& record = records[count++];

            record.pid   = static_cast<i32>(raw_id(value->pid()));
            record.ppid  = static_cast<i32>(raw_id(value->parent_pid()));
            record.state = static_cast<u32>(value->state());

            process::metadata_snapshot metadata{};

            value->snapshot_metadata(metadata);

            record.pgid     = static_cast<i32>(raw_id(metadata.process_group));
            record.uid      = metadata.uid;
            record.gid      = metadata.gid;
            record.has_ctty = metadata.controlling_terminal ? 1u : 0u;

            if (metadata.controlling_terminal &&
                !terminal_name(*metadata.controlling_terminal, record.ctty,
                               sizeof record.ctty)) {
                // A terminal without a VFS path is reported as unknown
                // rather than as an empty name.
                record.ctty[0] = '?';
                record.ctty[1] = '\0';
            }

            const char* name = value->name();

            for (u32 n = 0; name != nullptr && n + 1 < sizeof(record.name) && name[n];
                 ++n) {
                record.name[n] = name[n];
            }
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
        const long rc    = terminal->acquire_controlling_terminal(group);
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

        const u32 count = snapshot_registry(values);

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
        terminal_access_result terminal_background_access(process*      owner,
                                                          pgrp_t        foreground,
                                                          signal_number signal) {
            if (owner == nullptr || foreground == pgrp_t{} ||
                owner->process_group() == foreground)
                return terminal_access_result::allowed;

            const signal_mask bit  = signal_bit(signal);
            auto*             self = current_thread();

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

        if (foreground == pgrp_t{} || owner->process_group() == foreground)
            return terminal_access_result::allowed;

        const signal_mask bit  = signal_bit(signal_number::ttou);
        auto*             self = current_thread();
        const signal_mask blocked =
            self != nullptr ? self->signals().blocked() : owner->signals().blocked();

        if ((blocked & bit) != 0 ||
            owner->signals().disposition(signal_number::ttou).action ==
                signal_action::ignore)
            return terminal_access_result::allowed;

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

        const u32 value_count = snapshot_registry(values);

        process* target = self_target ? caller : nullptr;

        for (u32 i = 0; i < value_count && target == nullptr; ++i) {
            if (values[i]->pid() == pid)
                target = values[i].get();
        }

        // A freshly forked child can execute setpgid(0, ...) before another
        // registry scan observes its slot.  pid==0 is explicitly the caller,
        // so use that stable object directly.
        if (!is_live(target))
            return -ESRCH;

        if (target != caller && target->parent_pid() != caller->pid())
            return -ESRCH;

        if (target->session() != caller->session())
            return -EPERM;

        // POSIX rejects setpgid() for a session leader, including a
        // redundant request for its current process group.
        if (is_process_group_leader(target) && target->session() == target->pid())
            return -EPERM;

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

            if (!group_exists)
                return -EPERM;
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

    namespace {
        bool deliver_signal(process* target, signal_number value,
                            const signal_info& info) {
            if (!target->signals().send(value, info))
                return false;

            target->interrupt_signal_io(value);

            if (value == signal_number::kill) {
                target->signal_termination(signal_number::kill);
                target->request_kill();
            }
            else if (is_uncatchable(value) ||
                     target->signals().disposition(value).action ==
                         signal_action::default_action) {
                target->signal_state_change(value);
            }

            if (target->signals().has_unblocked_interrupting())
                sys::io_interrupt_process(target, -EINTR);

            target->wake_signal_waiters();

            return true;
        }

        i64 audience_error(signal_audience_status status) {
            switch (status) {
            case signal_audience_status::none:
                return -ESRCH;
            case signal_audience_status::denied:
                return -EPERM;
            case signal_audience_status::allowed:
                return 0;
            }

            return -ESRCH;
        }
    } // namespace

    i64 signal_process_group(pgrp_t group, signal_number value, signal_info info,
                             const signal_sender* sender) {
        if (!valid_signal(value) && value != signal_number{})
            return -EINVAL;

        mm::shptr<process> targets[max_processes];

        const u32 value_count = snapshot_registry(targets);
        u32       count       = 0;

        signal_audience audience;

        for (u32 i = 0; i < value_count; ++i) {
            process* target = targets[i].get();

            if (!is_live(target) || target->process_group() != group)
                continue;

            if (sender != nullptr &&
                !audience.admit(sender->uid, sender->euid, target->uid(), target->euid()))
                continue;

            targets[count++] = static_cast<mm::shptr<process>&&>(targets[i]);
        }

        if (value != signal_number{}) {
            for (u32 i = 0; i < count; ++i) {
                if (!deliver_signal(targets[i].get(), value, info))
                    return -EINVAL;
            }
        }

        if (sender != nullptr)
            return audience_error(audience.status());

        return count != 0 ? 0 : -ESRCH;
    }

    i64 signal_all_processes(signal_number value, signal_info info,
                             const signal_sender& sender, pid_t exclude) {
        if (!valid_signal(value) && value != signal_number{})
            return -EINVAL;

        mm::shptr<process> targets[max_processes];

        const u32 value_count = snapshot_registry(targets);
        u32       count       = 0;

        signal_audience audience;

        for (u32 i = 0; i < value_count; ++i) {
            process* target = targets[i].get();

            if (!is_live(target) || target->pid() == exclude)
                continue;

            if (!audience.admit(sender.uid, sender.euid, target->uid(), target->euid()))
                continue;

            targets[count++] = static_cast<mm::shptr<process>&&>(targets[i]);
        }

        if (value != signal_number{}) {
            for (u32 i = 0; i < count; ++i) {
                if (!deliver_signal(targets[i].get(), value, info))
                    return -EINVAL;
            }
        }

        return audience_error(audience.status());
    }

    namespace {
        /// Suspends a waitpid scan until a child changes state.  The caller
        /// masks interrupts so registration and the scan stay atomic against
        /// child exit; the awaiter arms the continuation and unmasks them
        /// only once the thread is parked.
        struct child_wait {
            thread& self;

            bool await_ready() const {
                return false;
            }

            bool await_suspend(std::coroutine_handle<> continuation) {
                if (!self.arm_wait_unless_cancelled(continuation)) {
                    arch::cpu::interrupts();

                    return false;
                }

                self.state(thread_state::blocked);
                arch::cpu::interrupts();

                return true;
            }

            void await_resume() {}
        };
    } // namespace

    core::task<i64> waitpid(i32 pid, i32* status, wait_options options) {
        process* parent = current_process();
        thread*  self   = current_thread();

        if (parent == nullptr || self == nullptr)
            co_return -ESRCH;

        const bool group_wait = pid < -1;

        const pgrp_t requested_group =
            group_wait ? wait_group_from_selector(pid)
                       : (pid == 0 ? parent->process_group() : pgrp_t{});

        if (raw_wait_options(options) & ~valid_wait_option_bits)
            co_return -EINVAL;

        i64 result = 0;

        while (true) {
            // From here to the suspension the scan and the waiter
            // registration must be atomic against child exits, which can
            // only happen by running the child on this single CPU.
            arch::cpu::nointerrupts();
            self->state(thread_state::blocked);

            if (!parent->add_waiter(self)) {
                // Never block without a slot: child exit would have no way
                // to wake this thread, leaving waitpid stuck forever.
                self->state(thread_state::running);

                result = -EAGAIN;

                break;
            }

            bool has_children = false;

            mm::shptr<process> taken;
            mm::shptr<process> children[max_processes];

            const u32 child_count = snapshot_registry(children);

            process* zombie_child = nullptr;

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
                    arch::cpu::interrupts();
                    continue;
                }

                // `taken` now owns the registry's reference; dropping it
                // below runs ~process, which frees the address space
                // and the dead threads' kernel stacks.  The child was
                // already switched off this CPU before we were resumed.
                const pid_t child_pid = taken->pid();

                if (status != nullptr)
                    *status = taken->wait_status();

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

            if (self->cancellation_pending() ||
                parent->signals().has_unblocked_interrupting_for(
                    self->signals().blocked())) {
                parent->remove_waiter(self);
                self->state(thread_state::running);

                result = -EINTR;

                break;
            }

            // A live child exists: park until thread_exited() wakes us.
            // The wake clears our waiter slot, so it is re-added at the
            // top of the next scan.
            co_await child_wait{*self};
        }

        arch::cpu::interrupts();

        co_return result;
    }
} // namespace astia::proc
