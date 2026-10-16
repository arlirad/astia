#include "proc.hpp"

#include "scheduler.hpp"
#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/log.hpp>
#include <astia/mm/heap.hpp>
#include <astia/mm/share.hpp>

void* operator new[](astia::usize size, void* where) noexcept;

namespace astia::proc {

    namespace {
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
            log::error("spawnve: open failed for %s", path);
            return fail(ENOENT);
        }
        const long image_size = image_file->size();
        if (image_size <= 0) {
            log::error("spawnve: invalid image size=%ld", image_size);
            return fail(ENOEXEC);
        }
        auto image = mm::heap::alloc(static_cast<usize>(image_size));
        if (!image) {
            return fail(ENOMEM);
        }
        const long loaded =
            image_file->read(image.value, static_cast<u32>(image_size), 0);
        if (loaded != image_size) {
            log::error("spawnve: short read=%ld expected=%ld", loaded, image_size);
            mm::heap::free(image.value);
            return fail(EIO);
        }

        const u32 child_id = next_pid++;
        auto      child    = mm::share<process>(child_id, parent->pid(), path);
        if (!child) {
            mm::heap::free(image.value);
            return fail(ENOMEM);
        }
        child->install_standard_streams();
        u64        entry = 0;
        const bool loaded_image =
            child->load_user_image(image.value, static_cast<usize>(image_size), &entry);
        mm::heap::free(image.value);
        if (!loaded_image || !child->create_user_thread(entry, argv, envp)) {
            log::error("spawnve: ELF load or user thread setup failed");
            return fail(ENOEXEC);
        }
        if (!register_process(child)) {
            log::error("spawnve: process registry full");
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

    i64 waitpid(i32 pid, i32* status) {
        process* parent = current_process();
        thread*  self   = current_thread();
        if (parent == nullptr || self == nullptr) {
            return -ESRCH;
        }
        if (pid < -1 || pid == 0) {
            return -EINVAL;
        }

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
                    *status = zombie_child->exit_status();
                }
                log::info("proc: reaped pid=%u status=%d", static_cast<u32>(child_pid),
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
