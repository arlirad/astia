#include "proc.hpp"

#include "scheduler.hpp"
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
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

        process* kernel   = nullptr;
        thread*  current  = nullptr;
        tid_t    next_tid = 1;
        pid_t    next_pid = 1;

        inline constexpr u32 registry_capacity = 64;
        // Non-trivial element type in freestanding code: static storage
        // plus placement construction, so the global itself stays
        // trivially destructible (same pattern as fs::devfs_instance).
        alignas(mm::shptr<process>) u8
            registry_storage[sizeof(mm::shptr<process>) * registry_capacity];
        mm::shptr<process>* registry = nullptr;
        core::spinlock      registry_lock;

        /// registry_lock must be held.
        i32 find_slot_locked(pid_t id) {
            for (u32 i = 0; i < registry_capacity; ++i) {
                if (registry[i] && registry[i]->pid() == id) {
                    return static_cast<i32>(i);
                }
            }
            return -1;
        }

        // registry_lock must be held.  Looking up the requested PID alone
        // would incorrectly allow setpgid() to join an arbitrary process
        // rather than an existing process group.
        process* find_group_locked(pid_t group, pid_t session) {
            for (u32 i = 0; i < registry_capacity; ++i) {
                process* value = registry[i].get();
                if (value != nullptr && value->process_group() == group &&
                    value->session() == session && value->state() != process_state::zombie &&
                    value->state() != process_state::dead) {
                    return value;
                }
            }
            return nullptr;
        }
    }

    bool register_process(const mm::shptr<process>& value) {
        core::spinlock_guard guard(registry_lock);
        for (u32 i = 1; i < registry_capacity; ++i) {
            if (!registry[i]) {
                registry[i] = value;
                return true;
            }
        }
        return false;
    }

    pid_t allocate_pid() {
        return next_pid++;
    }

    bool init() {
        if (kernel != nullptr) {
            return false;
        }

        registry    = ::new (registry_storage) mm::shptr<process>[registry_capacity];
        registry[0] = mm::share<process>(0, 0, "kernel");
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
        current = bootstrap.get();
        kernel->state(process_state::runnable);
        return true;
    }

    process* kernel_process() {
        return kernel;
    }

    process* current_process() {
        return current == nullptr ? nullptr : current->owner();
    }

    thread* current_thread() {
        return current;
    }

    tid_t allocate_tid() {
        return next_tid++;
    }

    void set_current_thread(thread* value) {
        current = value;
    }

    mm::shptr<process> spawnve(const char* path, char* const argv[], char* const envp[],
                               int* error) {
        // Every failure path hands the syscall boundary its negated errno.
        const auto fail = [error](int code) -> mm::shptr<process> {
            if (error != nullptr) {
                *error = -code;
            }
            return {};
        };
        process* parent = current_process();
        if (parent == nullptr || path == nullptr) {
            return fail(EINVAL);
        }
        auto image_file = fs::open_path(path);
        if (!image_file) {
            spawn_logs::error("open failed for %s", path);
            return fail(ENOENT);
        }
        const long image_size = image_file->size();
        if (image_size <= 0) {
            spawn_logs::error("invalid image size=%ld", image_size);
            return fail(ENOEXEC);
        }
        auto image = mm::heap::alloc(static_cast<usize>(image_size));
        if (!image) {
            spawn_logs::error("image allocation failed size=%ld", image_size);
            return fail(ENOMEM);
        }
        const long loaded =
            image_file->read(image.value, static_cast<u32>(image_size), 0);
        if (loaded != image_size) {
            spawn_logs::error("short read=%ld expected=%ld", loaded, image_size);
            mm::heap::free(image.value);
            return fail(EIO);
        }

        const u32 child_id = next_pid++;
        auto      child    = mm::share<process>(child_id, parent->pid(), path);
        if (!child) {
            mm::heap::free(image.value);
            return fail(ENOMEM);
        }
        child->inherit_groups(*parent);
        child->install_standard_streams();
        u64        entry = 0;
        const bool loaded_image =
            child->load_user_image(image.value, static_cast<usize>(image_size), &entry);
        mm::heap::free(image.value);
        if (!loaded_image || !child->create_user_thread(entry, argv, envp)) {
            spawn_logs::error("ELF load or user thread setup failed");
            return fail(ENOEXEC);
        }
        if (!register_process(child)) {
            spawn_logs::error("process registry full");
            return fail(EAGAIN);
        }
        child->state(process_state::runnable);
        return child;
    }

    process* find_process(pid_t id) {
        core::spinlock_guard guard(registry_lock);
        const i32            slot = find_slot_locked(id);
        return slot < 0 ? nullptr : registry[slot].get();
    }

    pid_t getpgrp() {
        process* owner = current_process();
        return owner == nullptr ? 0 : owner->process_group();
    }

    i64 setpgid(i32 pid, i32 pgid) {
        process* caller = current_process();
        if (caller == nullptr) return -ESRCH;
        if (pid == 0) pid = static_cast<i32>(caller->pid());
        if (pgid == 0) pgid = pid;
        if (pid <= 0 || pgid <= 0) return -EINVAL;

        core::spinlock_guard guard(registry_lock);
        const i32             slot = find_slot_locked(static_cast<pid_t>(pid));
        process*              target = slot < 0 ? nullptr : registry[slot].get();
        if (target == nullptr || target->state() == process_state::zombie ||
            target->state() == process_state::dead) {
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
        if (target->session() == target->pid()) {
            return -EPERM;
        }

        if (static_cast<pid_t>(pgid) != target->pid()) {
            if (find_group_locked(static_cast<pid_t>(pgid), caller->session()) == nullptr) {
                return -EPERM;
            }
        }
        target->process_group(static_cast<pid_t>(pgid));
        return 0;
    }

    i64 setsid() {
        process* owner = current_process();
        if (owner == nullptr) return -ESRCH;
        if (owner->process_group() == owner->pid()) return -EPERM;
        owner->session(owner->pid());
        owner->process_group(owner->pid());
        return static_cast<i64>(owner->session());
    }

    i64 signal_process_group(pid_t group, i32 value) {
        if (value < 0 || value >= static_cast<i32>(signal_count)) return -EINVAL;
        bool found = false;
        core::spinlock_guard guard(registry_lock);
        for (u32 i = 0; i < registry_capacity; ++i) {
            process* target = registry[i].get();
            if (target == nullptr || target->process_group() != group ||
                target->state() == process_state::zombie ||
                target->state() == process_state::dead)
                continue;
            found = true;
            if (value != 0 && !target->signals().send(static_cast<signal_number>(value)))
                return -EINVAL;
        }
        return found ? 0 : -ESRCH;
    }

    i64 waitpid(i32 pid, i32* status, u32 options) {
        process* parent = current_process();
        thread*  self   = current_thread();
        logs::info("waitpid parent=%d tid=%d target=%d",
                   parent ? static_cast<int>(parent->pid()) : -1,
                   self ? static_cast<int>(self->tid()) : -1, pid);
        if (parent == nullptr || self == nullptr) {
            return -ESRCH;
        }
        if (pid < -1 || pid == 0) {
            return -EINVAL;
        }
        if (options & ~(ASTIA_WNOHANG | ASTIA_WUNTRACED | ASTIA_WCONTINUED)) return -EINVAL;

        // From here to the blocking yield the scan and the waiter
        // registration must be atomic against child exits, which can
        // only happen by running the child on this single CPU.
        arch::cpu::nointerrupts();
        i64 result = 0;
        for (;;) {
            parent->add_waiter(self);

            process*           zombie_child = nullptr;
            bool               has_children = false;
            mm::shptr<process> taken;
            {
                core::spinlock_guard guard(registry_lock);
                for (u32 i = 0; i < registry_capacity; ++i) {
                    process* child = registry[i].get();
                    if (child == nullptr || child->parent_pid() != parent->pid()) {
                        continue;
                    }
                    if (pid > 0 && child->pid() != static_cast<pid_t>(pid)) {
                        continue;
                    }
                    has_children = true;
                    if (child->state() == process_state::zombie) {
                        taken        = registry[i];
                        registry[i]  = {};
                        zombie_child = child;
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
                logs::info("reaped pid=%u status=%d", static_cast<u32>(child_pid),
                           status == nullptr ? zombie_child->exit_status() : *status);
                result = static_cast<i64>(child_pid);
                parent->remove_waiter(self);
                taken = {};
                break;
            }
            if (!has_children) {
                parent->remove_waiter(self);
                result = -ECHILD;
                break;
            }
            if (options & ASTIA_WNOHANG) {
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
