#include <astia/api/syscalls.h>
#include "core/spinlock.hpp"
#include "frame_allocator.hpp"
#include "share.hpp"
#include "proc.hpp"
#include "process.hpp"
#include "scheduler.hpp"

void* operator new[](astia::usize size, void* where) noexcept;

namespace astia::proc {

    namespace {
        core::spinlock process_lock;

        inline constexpr u64 thread_tls_first = 0x00007fffef000000ull;
        inline constexpr u64 thread_tls_stride = 0x00100000ull;

        inline constexpr u32 dead_capacity = 16;
        // Placement-constructed like the process registry: keeps the
        // global trivially destructible in freestanding code.
        alignas(mm::shptr<thread>)
            u8                dead_storage[sizeof(mm::shptr<thread>) * dead_capacity];
        mm::shptr<thread>* dead_queue = nullptr;
        u32                 dead_count = 0;
    }

    auto defer_dead_thread(const mm::shptr<thread>& value) -> void {
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
    auto reap_dead_threads() -> void {
        if (dead_queue == nullptr) {
            return;
        }
        mm::shptr<thread> pending[dead_capacity];
        u32               count = 0;
        {
            core::spinlock_guard guard(process_lock);
            for (u32 i = 0; i < dead_count; ++i) {
                pending[i] = dead_queue[i];
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
        auto cstr_length(const char* value, usize limit) -> usize {
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
        auto write_user(u64 base, const u64* frames, u32 frame_count, u64 vaddr,
                        const void* src, usize length) -> bool {
            const auto* bytes = static_cast<const u8*>(src);
            while (length != 0) {
                if (vaddr < base) {
                    return false;
                }
                const u64 offset = vaddr - base;
                if (offset / mm::frame_allocator::frame_size >= frame_count) {
                    return false;
                }
                auto* page = static_cast<u8*>(
                    mm::kernel_frames().to_virt(frames[offset /
                                                       mm::frame_allocator::frame_size]));
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

        auto write_user_u64(u64 base, const u64* frames, u32 frame_count, u64 vaddr,
                            u64 value) -> bool {
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
        auto build_initial_stack(u64 base, u64 top, const u64* frames, u32 frame_count,
                                 const char* const* argv, u32 argc,
                                 const char* const* envp, u32 envc, u64* rsp_out) -> bool {
            u64 cursor = top;
            u64 argv_address[max_spawn_args]{};
            u64 envp_address[max_spawn_env]{};

            // Strings first, packed from the top down.
            for (u32 i = envc; i > 0; --i) {
                const char* text = envp[i - 1];
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
                const char* text = argv[i - 1];
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
            const u64 block =
                sizeof(u64) * (1 + argc + 1 + envc + 1 + 2);
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
        : pid_(id), parent_pid_(parent) {
        u32 i = 0;
        while (name != nullptr && name[i] != '\0' && i + 1 < sizeof(name_)) {
            name_[i] = name[i];
            ++i;
        }
        name_[i] = '\0';
    }

    process::~process() {
        // Remove every VMA's page coverage first.  Sibling VMAs may
        // intentionally share one allocation reference (`holds_ref`),
        // so object lifetime and page coverage are released separately.
        while (mmap_regions_ != nullptr) {
            mmap_region* victim = mmap_regions_;
            mmap_regions_       = victim->next;
            victim->backing->unmap_range(victim->region_page, victim->pages);
            if (victim->holds_ref) {
                victim->backing->release_ref();
            }
            delete victim;
        }
        delete address_space_;
    }

    auto process::add_thread(const mm::shptr<thread>& value) -> bool {
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

    auto process::thread_at(u32 index) const -> mm::shptr<thread> {
        return index < max_process_threads ? threads_[index] : mm::shptr<thread>{};
    }

    auto process::thread_exited(thread* value, i32 status) -> void {
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
            parent->wake_waiters();
        }
    }

    auto process::take_thread(thread* value) -> mm::shptr<thread> {
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

    auto process::release_thread(thread* value) -> void {
        core::spinlock_guard guard(process_lock);
        for (u32 i = 0; i < max_process_threads; ++i) {
            if (threads_[i] && threads_[i].get() == value) {
                threads_[i] = {};
                if (thread_count_ != 0) {
                    --thread_count_;
                }
                return;
            }
        }
    }

    auto process::add_waiter(thread* value) -> bool {
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

    auto process::remove_waiter(thread* value) -> void {
        core::spinlock_guard guard(process_lock);
        for (u32 i = 0; i < max_process_waiters; ++i) {
            if (waiters_[i] == value) {
                waiters_[i] = nullptr;
            }
        }
    }

    auto process::wake_waiters() -> void {
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

    auto process::install_standard_streams() -> bool {
        return descriptors_.install_standard_streams();
    }

    auto process::descriptor(i32 fd) const -> mm::shptr<fs::open_file> {
        return descriptors_.get(fd);
    }

    auto process::set_descriptor(i32 fd, const mm::shptr<fs::open_file>& value) -> bool {
        return descriptors_.set(fd, value);
    }

    auto process::clear_descriptor(i32 fd) -> bool {
        return descriptors_.clear(fd);
    }

    auto process::duplicate_descriptor(i32 old_fd) -> i32 {
        return descriptors_.duplicate(old_fd);
    }

    auto process::duplicate_descriptor_to(i32 old_fd, i32 new_fd) -> i32 {
        return descriptors_.duplicate_to(old_fd, new_fd);
    }

    auto process::create_user_thread(u64 entry, char* const argv[], char* const envp[])
        -> mm::shptr<thread> {
        if (address_space_ == nullptr) {
            return {};
        }
        constexpr u64  stack_top   = 0x00007ffffff000ull;
        constexpr u64  stack_size  = 16 * 1024;
        constexpr u32  stack_pages = static_cast<u32>(stack_size / 4096);
        u64            frames[stack_pages]{};
        const u64      stack_base = stack_top - stack_size;
        // One contiguous run, so the stack's VMA can adopt it as a
        // single phys_region — the unit fork's copy-on-write
        // accounting works in.
        const u64 run =
            mm::kernel_frames().alloc(stack_pages, mm::frame_flags::zeroed);
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
            !register_mapping(stack_base, stack_pages, PROT_READ | PROT_WRITE,
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
            !add_thread(value) ||
            !scheduler::wake(value.get())) {
            return {};
        }
        return value;
    }

    auto process::create_thread(u64 entry, u64 user_stack_top, u64 argument)
        -> mm::shptr<thread> {
        if (address_space_ == nullptr || entry == 0 || user_stack_top == 0) {
            return {};
        }
        const tid_t id = proc::allocate_tid();
        u64          fs_base = tls_tp_;
        u64          tls_base = 0;
        usize        tls_pages = 0;
        if (tls_tp_ != 0 && !clone_tls_for_thread(id, fs_base, tls_base, tls_pages)) {
            return {};
        }
        auto value = mm::share<thread>(this, id);
        if (!value ||
            !value->prepare_user_start(entry, user_stack_top, fs_base, argument) ||
            !add_thread(value) || !scheduler::wake(value.get())) {
            return {};
        }
        value->tls_mapping(tls_base, tls_pages);
        return value;
    }

    auto process::clone_tls_for_thread(tid_t id, u64& fs_base,
                                       u64& mapping_base,
                                       usize& mapping_pages) -> bool {
        if (tls_tp_ == 0) {
            mapping_base = 0;
            mapping_pages = 0;
            return true;
        }
        const u64 page_bytes = mm::phys_region::frame_size;
        mmap_region* source = nullptr;
        for (mmap_region* it = mmap_regions_; it != nullptr; it = it->next) {
            if (it->covers(tls_tp_ - 1) && it->backing != nullptr) {
                source = it;
                break;
            }
        }
        if (source == nullptr) {
            return false;
        }

        u64 base = thread_tls_first -
                   (static_cast<u64>(id % max_process_threads) * thread_tls_stride);
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
            base -= thread_tls_stride;
        }
        if (!free) {
            return false;
        }
        auto* copy = mm::phys_region::create(source->pages, false);
        if (copy == nullptr) {
            return false;
        }
        for (usize page = 0; page < source->pages; ++page) {
            __builtin_memcpy(copy->page_direct(page),
                             source->backing->page_direct(page), page_bytes);
            if (!address_space_->map_4k(base + page * page_bytes,
                                        copy->base() + page * page_bytes,
                                        arch::page_flags::present |
                                            arch::page_flags::writable |
                                            arch::page_flags::user)) {
                for (usize done = 0; done < page; ++done) {
                    address_space_->unmap(base + done * page_bytes);
                }
                copy->release();
                return false;
            }
        }
        if (!register_mapping(base, source->pages, PROT_READ | PROT_WRITE, copy)) {
            for (usize page = 0; page < source->pages; ++page) {
                address_space_->unmap(base + page * page_bytes);
            }
            copy->release();
            return false;
        }
        mapping_base  = base;
        mapping_pages = source->pages;
        fs_base       = base + (tls_tp_ - source->base);
        return true;
    }

    auto process::thread_by_tid(tid_t id) -> mm::shptr<thread> {
        core::spinlock_guard guard(process_lock);
        for (u32 i = 0; i < max_process_threads; ++i) {
            if (threads_[i] && threads_[i]->tid() == id) {
                return threads_[i];
            }
        }
        return {};
    }

    auto process::copy_descriptors_from(const process& other) -> bool {
        for (u32 fd = 0; fd < max_process_fds; ++fd) {
            auto entry = other.descriptors_.get(static_cast<i32>(fd));
            if (entry && !descriptors_.set(static_cast<i32>(fd), entry)) {
                return false;
            }
        }
        return true;
    }

    auto process::create_fork_thread(const arch::user_context& uctx)
        -> mm::shptr<thread> {
        auto value = mm::share<thread>(this, proc::allocate_tid());
        if (!value || !value->prepare_fork_user(uctx)) {
            return {};
        }
        if (!add_thread(value) || !scheduler::wake(value.get())) {
            return {};
        }
        return value;
    }

} // namespace astia::proc
