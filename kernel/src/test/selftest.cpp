#include "selftest.hpp"

#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/atomic.hpp>
#include <astia/common.hpp>
#include <astia/error.hpp>
#include <astia/fs.hpp>
#include <astia/log.hpp>
#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/phys_region.hpp>

#include "../dev/chr/tty/uart16550.hpp"
#include "../fs/filesystem.hpp"
#include "../proc/task_run.hpp"
#include "../proc/tls_arena.hpp"
#include "aio.hpp"
#include "proc.hpp"
#include "scheduler.hpp"
#include "selftest_arch.hpp"
#include "syscall.hpp"

namespace astia::test {

    namespace {

        /// This suite's log source.
        using logs = astia::log::context<"selftest">;

        u32 scheduler_counts[2]{};
        u32 scheduler_gate_result = 0;
        u32 scheduler_wake_phase  = 0;
        u32 scheduler_wake_result = 0;
        u32 preempt_started       = 0;
        u32 preempt_main_seen     = 0;
        u32 preempt_done          = 0;

        class registry_test_mount final : public fs::mount {};

        class registry_test_filesystem final : public fs::filesystem {
            public:
            const char* name() const override {
                return "registry-test";
            }
            fs::mount* mount(const char*) const override {
                if (registration_ != nullptr && factory_unregistration_ != nullptr)
                    *factory_unregistration_ = fs::unregister_type(*registration_);
                return &mount_;
            }
            void observe(fs::filesystem_registration* registration, long* result) {
                registration_           = registration;
                factory_unregistration_ = result;
            }

            private:
            mutable fs::filesystem_registration* registration_           = nullptr;
            mutable long*                        factory_unregistration_ = nullptr;
            mutable registry_test_mount          mount_;
        };

        void scheduler_worker(void* argument) {
            auto* count = static_cast<u32*>(argument);
            for (u32 i = 0; i < 3; ++i) {
                astia::atomic_ref<u32>(*count).add_fetch(1, astia::memory_order::relaxed);
                proc::scheduler::yield();
            }
        }

        void scheduler_masked_worker(void*) {
            proc::current_thread()->charge_runtime(1ull << 60);
            arch::cpu::nointerrupts();
            proc::scheduler::yield();
            astia::atomic_ref<u32>(scheduler_gate_result)
                .store(arch::cpu::check_interrupts() ? 2u : 1u,
                       astia::memory_order::release);
            arch::cpu::interrupts();
        }

        void scheduler_wake_worker(void*) {
            arch::cpu::nointerrupts();
            proc::current_thread()->state(proc::thread_state::blocked);
            astia::atomic_ref<u32>(scheduler_wake_phase)
                .store(1u, astia::memory_order::release);
            proc::scheduler::yield();
            astia::atomic_ref<u32>(scheduler_wake_result)
                .store(arch::cpu::check_interrupts() ? 2u : 1u,
                       astia::memory_order::release);
            arch::cpu::interrupts();
        }

        void scheduler_waker(void* argument) {
            auto* target = static_cast<proc::thread*>(argument);
            while (astia::atomic_ref<u32>(scheduler_wake_phase)
                       .load(astia::memory_order::acquire) == 0)
                proc::scheduler::yield();
            if (!proc::scheduler::wake(target)) {
                astia::atomic_ref<u32>(scheduler_wake_result)
                    .store(3u, astia::memory_order::release);
            }
            proc::current_thread()->exit();
        }

        bool scheduler_checks() {
            const bool gate_open = arch::cpu::check_interrupts();
            arch::cpu::interrupts();
            astia::atomic_ref<u32>(scheduler_gate_result)
                .store(0, astia::memory_order::relaxed);
            if (!proc::scheduler::spawn_kernel(scheduler_masked_worker, nullptr)) {
                if (!gate_open) {
                    arch::cpu::nointerrupts();
                }
                logs::error("scheduler: could not spawn masked worker");
                return false;
            }
            proc::scheduler::yield();
            if (!arch::cpu::check_interrupts()) {
                if (!gate_open) {
                    arch::cpu::nointerrupts();
                }
                logs::error("scheduler: gate restore open=0");
                return false;
            }
            proc::scheduler::yield();
            const u32 masked_result = astia::atomic_ref<u32>(scheduler_gate_result)
                                          .load(astia::memory_order::acquire);
            if (masked_result != 1) {
                if (!gate_open) {
                    arch::cpu::nointerrupts();
                }
                logs::error("scheduler: worker gate=%u", masked_result);
                return false;
            }
            astia::atomic_ref<u32>(scheduler_wake_phase)
                .store(0, astia::memory_order::relaxed);
            astia::atomic_ref<u32>(scheduler_wake_result)
                .store(0, astia::memory_order::relaxed);
            auto wake_target = proc::scheduler::create_kernel(
                scheduler_wake_worker, nullptr, proc::scheduling_class::realtime, 0, 1);
            if (!wake_target ||
                !proc::scheduler::spawn_kernel(scheduler_waker, wake_target.get())) {
                if (!gate_open) {
                    arch::cpu::nointerrupts();
                }
                logs::error("scheduler: could not spawn wake workers");
                return false;
            }
            for (u32 i = 0; i < 16 && astia::atomic_ref<u32>(scheduler_wake_result)
                                              .load(astia::memory_order::acquire) == 0;
                 ++i) {
                proc::scheduler::yield();
            }
            const u32 wake_result = astia::atomic_ref<u32>(scheduler_wake_result)
                                        .load(astia::memory_order::acquire);
            if (wake_result != 1) {
                if (!gate_open) {
                    arch::cpu::nointerrupts();
                }
                logs::error("scheduler: wake gate=%u", wake_result);
                return false;
            }
            if (wake_target->detach() != 0) {
                if (!gate_open) {
                    arch::cpu::nointerrupts();
                }
                logs::error("scheduler: wake target detach failed");
                return false;
            }
            astia::atomic_ref<u32>(scheduler_counts[0])
                .store(0, astia::memory_order::relaxed);
            astia::atomic_ref<u32>(scheduler_counts[1])
                .store(0, astia::memory_order::relaxed);
            if (!proc::scheduler::spawn_kernel(scheduler_worker,
                                               const_cast<u32*>(&scheduler_counts[0])) ||
                !proc::scheduler::spawn_kernel(scheduler_worker,
                                               const_cast<u32*>(&scheduler_counts[1]))) {
                if (!gate_open) {
                    arch::cpu::nointerrupts();
                }
                logs::error("scheduler: could not spawn kernel threads");
                return false;
            }
            for (u32 i = 0; i < 16 && (astia::atomic_ref<u32>(scheduler_counts[0])
                                               .load(astia::memory_order::relaxed) != 3 ||
                                       astia::atomic_ref<u32>(scheduler_counts[1])
                                               .load(astia::memory_order::relaxed) != 3);
                 ++i) {
                proc::scheduler::yield();
            }
            const u32 first = astia::atomic_ref<u32>(scheduler_counts[0])
                                  .load(astia::memory_order::relaxed);
            const u32 second = astia::atomic_ref<u32>(scheduler_counts[1])
                                   .load(astia::memory_order::relaxed);
            if (!gate_open) {
                arch::cpu::nointerrupts();
            }
            if (first != 3 || second != 3) {
                logs::error("scheduler: counts=%u,%u", first, second);
                return false;
            }
            logs::info("scheduler ok");
            return true;
        }

        void preempt_worker(void*) {
            astia::atomic_ref<u32>(preempt_started)
                .store(1, astia::memory_order::release);
            while (astia::atomic_ref<u32>(preempt_main_seen)
                       .load(astia::memory_order::acquire) == 0) {
                // Deliberately no yield: only timer preemption can return
                // control to the bootstrap thread.
            }
            astia::atomic_ref<u32>(preempt_done).store(1, astia::memory_order::release);
        }

        void join_worker(void*) {
            proc::current_thread()->exit(reinterpret_cast<void*>(0x42));
        }

        core::task<i64> join_task(proc::thread& target, void** result) {
            auto joined = co_await target.join(result);
            if (!joined)
                co_return error_errno(joined.error);
            co_return joined.value;
        }

        long join_blocking(proc::thread& target, void** result = nullptr) {
            return static_cast<long>(proc::run_task(join_task(target, result)));
        }

        bool join_checks() {
            if (join_blocking(*proc::current_thread()) != -EDEADLK) {
                logs::error("join: self-join was not rejected");
                return false;
            }
            auto target = proc::scheduler::create_kernel(join_worker, nullptr);
            if (!target) {
                logs::error("join: could not spawn kernel thread");
                return false;
            }
            void*      result = nullptr;
            const long rc     = join_blocking(*target, &result);
            if (rc != 0 || result != reinterpret_cast<void*>(0x42)) {
                logs::error("join: failed rc=%ld result=%p", rc, result);
                return false;
            }
            if (join_blocking(*target) != -EINVAL) {
                logs::error("join: double join was not rejected");
                return false;
            }

            auto exited = proc::scheduler::create_kernel(join_worker, nullptr);
            if (!exited) {
                logs::error("join: could not spawn pre-exited thread");
                return false;
            }
            proc::scheduler::yield();
            result = nullptr;
            if (join_blocking(*exited, &result) != 0 ||
                result != reinterpret_cast<void*>(0x42)) {
                logs::error("join: pre-exited target was not joinable");
                return false;
            }

            auto detached = proc::scheduler::create_kernel(join_worker, nullptr);
            if (!detached || detached->detach() != 0 ||
                join_blocking(*detached) != -EINVAL) {
                logs::error("join: detached join was not rejected");
                return false;
            }
            proc::scheduler::yield();
            logs::info("join ok");
            return true;
        }

        bool preemption_checks() {
            astia::atomic_ref<u32>(preempt_started)
                .store(0, astia::memory_order::relaxed);
            astia::atomic_ref<u32>(preempt_main_seen)
                .store(0, astia::memory_order::relaxed);
            astia::atomic_ref<u32>(preempt_done).store(0, astia::memory_order::relaxed);
            if (!proc::scheduler::spawn_kernel(preempt_worker, nullptr)) {
                logs::error("preemption: could not spawn kernel thread");
                return false;
            }

            arch::cpu::interrupts();
            for (u32 i = 0; i < 200 && astia::atomic_ref<u32>(preempt_started)
                                               .load(astia::memory_order::acquire) == 0;
                 ++i) {
                arch::cpu::halt();
            }
            if (astia::atomic_ref<u32>(preempt_started)
                    .load(astia::memory_order::acquire) == 0) {
                arch::cpu::nointerrupts();
                logs::error("preemption: did not switch to worker");
                return false;
            }
            astia::atomic_ref<u32>(preempt_main_seen)
                .store(1, astia::memory_order::release);
            for (u32 i = 0; i < 200 && astia::atomic_ref<u32>(preempt_done)
                                               .load(astia::memory_order::acquire) == 0;
                 ++i) {
                arch::cpu::halt();
            }
            arch::cpu::nointerrupts();
            if (astia::atomic_ref<u32>(preempt_done).load(astia::memory_order::acquire) ==
                0) {
                logs::error("preemption: worker did not finish");
                return false;
            }
            logs::info("preemption ok");
            return true;
        }

        bool uname_checks() {
            static_assert(static_cast<u32>(sys::syscall_id::fchdir) == 69);
            static_assert(static_cast<u32>(sys::syscall_id::uname) == 70);
            static_assert(static_cast<u32>(sys::syscall_id::dlmap) == 71);
            static_assert(static_cast<u32>(sys::syscall_id::dlunmap) == 72);
            static_assert(static_cast<u32>(sys::syscall_id::modopen) == 73);
            static_assert(static_cast<u32>(sys::syscall_id::modls) == 74);
            static_assert(static_cast<u32>(sys::syscall_id::modstat) == 75);
            static_assert(static_cast<u32>(sys::syscall_id::modclose) == 76);
            static_assert(SYS_FCHDIR == 69);
            static_assert(SYS_UNAME == 70);
            static_assert(SYS_DLMAP == 71);
            static_assert(SYS_DLUNMAP == 72);
            static_assert(SYS_MODOPEN == 73);
            static_assert(SYS_MODLS == 74);
            static_assert(SYS_MODSTAT == 75);
            static_assert(SYS_MODCLOSE == 76);
            auto call = [](u64 output) -> long {
                arch::syscall_return image{};
                return static_cast<long>(sys::dispatch(sys::syscall_id::uname,
                                                       static_cast<i64>(output), 0, 0, 0,
                                                       0, 0, image));
            };
            auto equal = [](const char* field, const char* value) {
                for (usize i = 0;; ++i) {
                    if (field[i] != value[i])
                        return false;
                    if (value[i] == '\0')
                        return true;
                }
            };
            auto padded = [](const char* field, usize size) {
                usize i = 0;
                while (i < size && field[i] != '\0')
                    ++i;
                if (i == size)
                    return false;
                while (++i < size) {
                    if (field[i] != '\0')
                        return false;
                }
                return true;
            };

            astia_utsname name;
            for (usize i = 0; i < sizeof name; ++i)
                reinterpret_cast<u8*>(&name)[i] = 0xA5;
            if (call(reinterpret_cast<u64>(&name)) != 0 ||
                !equal(name.sysname, "Astia") || !equal(name.nodename, "astia") ||
                name.release[0] == '\0' || name.version[0] == '\0' ||
                !equal(name.machine, "x86_64") ||
                !padded(name.sysname, sizeof name.sysname) ||
                !padded(name.nodename, sizeof name.nodename) ||
                !padded(name.release, sizeof name.release) ||
                !padded(name.version, sizeof name.version) ||
                !padded(name.machine, sizeof name.machine)) {
                logs::error("uname fail (identity)");
                return false;
            }
            if (call(0) != -EFAULT) {
                logs::error("uname fail (null)");
                return false;
            }
            arch::syscall_return unassigned{};
            if (sys::dispatch(static_cast<sys::syscall_id>(77), 0, 0, 0, 0, 0, 0,
                              unassigned) != -ENOSYS) {
                logs::error("syscall id 77 is not unassigned");
                return false;
            }
            logs::info("uname ok");
            return true;
        }

        // SYS_CLOCK_GETTIME is dispatched through the production descriptor registry.
        // The timer tick is the resolution, so samples need only remain in range
        // and must not run backwards.
        bool clock_checks() {
            // The dispatcher takes what the entry would have handed it:
            // the call number, the six registers, and the image it packed
            // them into. clock_gettime reads registers and never looks at
            // the image, but the image is still the real one a trap for
            // this call would have made.
            auto read = [](i64 id, astia_timespec* out) -> long {
                arch::syscall_return image{};
                return static_cast<long>(sys::dispatch(
                    sys::syscall_id::clock_gettime, id,
                    static_cast<i64>(reinterpret_cast<u64>(out)), 0, 0, 0, 0, image));
            };

            astia_timespec first{};
            astia_timespec second{};
            astia_timespec wall{};

            if (read(CLOCK_MONOTONIC, &first) != 0 || first.tv_nsec < 0 ||
                first.tv_nsec >= 1000000000) {
                logs::error("clock fail (monotonic)");
                return false;
            }
            if (read(CLOCK_REALTIME, &wall) != 0 || wall.tv_sec < 1000000000) {
                // Wall time is uptime anchored at a fixed epoch: it must be
                // far past 2001, unlike the uptime reading next to it.
                logs::error("clock fail (realtime)");
                return false;
            }
            if (read(CLOCK_MONOTONIC, &second) != 0 || second.tv_sec < first.tv_sec ||
                (second.tv_sec == first.tv_sec && second.tv_nsec < first.tv_nsec)) {
                logs::error("clock fail (regression)");
                return false;
            }
            if (read(99, &first) != -EINVAL) {
                logs::error("clock fail (bad clock id)");
                return false;
            }

            logs::info("clock ok");
            return true;
        }

        // A VFS node with a real size, so STATX has a success path whether
        // or not an initrd is mounted on this boot.
        struct sized_probe : fs::file, fs::readable, fs::writable {
            mutable u32 stat_calls   = 0;
            u32         submit_calls = 0;

            optional<fs::readable&> as_readable() override {
                return *this;
            }
            optional<fs::writable&> as_writable() override {
                return *this;
            }
            core::task<i64> readv(span<const fs::buffer> buffers, u64 offset,
                                  core::cltoken token) override {
                return submit_readv(buffers, offset, token);
            }
            core::task<i64> writev(span<const fs::buffer> buffers, u64 offset,
                                   core::cltoken token) override {
                return submit_writev(buffers, offset, token);
            }

            long size() const override {
                ++stat_calls;
                return 4096;
            }

            long submit(io::request& request) override {
                ++submit_calls;
                return static_cast<long>(request.length());
            }
        };
        sized_probe probe_node{};

        struct parked_probe : fs::file, fs::readable, fs::writable {
            bool ready        = false;
            u32  submit_calls = 0;

            optional<fs::readable&> as_readable() override {
                return *this;
            }
            optional<fs::writable&> as_writable() override {
                return *this;
            }
            core::task<i64> readv(span<const fs::buffer> buffers, u64 offset,
                                  core::cltoken token) override {
                return submit_readv(buffers, offset, token);
            }
            core::task<i64> writev(span<const fs::buffer> buffers, u64 offset,
                                   core::cltoken token) override {
                return submit_writev(buffers, offset, token);
            }

            long submit(io::request& request) override {
                ++submit_calls;
                if (!ready)
                    return 0;
                if (request.direction() == io::direction::input && request.length() != 0)
                    static_cast<u8*>(request.buffer())[0] = 0x5a;
                return static_cast<long>(request.length());
            }
        } parked_node{};

        bool aio_capacity_boundary_checks() {
            astia_rings* rings[ASTIA_AIO_MAX_RING_SLOTS]{};
            u32          ring_count = 0;
            for (; ring_count < ASTIA_AIO_MAX_RING_SLOTS; ++ring_count) {
                if (sys::io_setup(1, 0, &rings[ring_count]) < 0 ||
                    rings[ring_count] == nullptr) {
                    logs::error("aio capacity fail (ring allocation)");
                    for (u32 i = 0; i < ring_count; ++i)
                        (void) sys::io_destroy(rings[i]);
                    return false;
                }
            }
            astia_rings* extra = nullptr;
            if (sys::io_setup(1, 0, &extra) != -ENFILE || extra != nullptr) {
                logs::error("aio capacity fail (seventeenth ring)");
                for (astia_rings* ring : rings)
                    (void) sys::io_destroy(ring);
                if (extra != nullptr)
                    (void) sys::io_destroy(extra);
                return false;
            }
            for (astia_rings* ring : rings)
                if (sys::io_destroy(ring) != 0) {
                    logs::error("aio capacity fail (ring teardown)");
                    return false;
                }

            struct capacity_probe : fs::file, fs::readable, fs::writable {
                bool ready        = false;
                u32  submit_calls = 0;

                optional<fs::readable&> as_readable() override {
                    return *this;
                }
                optional<fs::writable&> as_writable() override {
                    return *this;
                }
                core::task<i64> readv(span<const fs::buffer> buffers, u64 offset,
                                      core::cltoken token) override {
                    return submit_readv(buffers, offset, token);
                }
                core::task<i64> writev(span<const fs::buffer> buffers, u64 offset,
                                       core::cltoken token) override {
                    return submit_writev(buffers, offset, token);
                }

                long submit(io::request& request) override {
                    ++submit_calls;
                    if (!ready)
                        return 0;
                    if (request.direction() == io::direction::input &&
                        request.length() != 0)
                        static_cast<u8*>(request.buffer())[0] = 0x6b;
                    return static_cast<long>(request.length());
                }
            } node;
            static const char path[] = "/dev/aio_capacity_probe";
            if (fs::register_device(path, mm::shptr<fs::file>::borrowed(&node)) < 0) {
                logs::error("aio capacity fail (device registration)");
                return false;
            }

            astia_rings* ring    = nullptr;
            auto*        current = proc::current_process();
            fd_t         fd{-1};
            auto         cleanup = [&]() {
                node.ready = true;
                if (ring != nullptr) {
                    sys::io_device_ready_context(&node);
                    while (astia_cq_ready(ring) != 0)
                        astia_cq_consume(ring, astia_cq_ready(ring));
                    (void) sys::io_submit(ring, 0);
                    (void) sys::io_destroy(ring);
                }
                if (current != nullptr && fd.value >= 0)
                    current->clear_descriptor(fd);
            };
            if (current == nullptr ||
                sys::io_setup(ASTIA_AIO_MAX_RING_ENTRIES, 0, &ring) < 0 ||
                ring == nullptr) {
                logs::error("aio capacity fail (park ring)");
                cleanup();
                return false;
            }
            auto file = fs::open(path);
            if (!file) {
                logs::error("aio capacity fail (park open)");
                cleanup();
                return false;
            }
            for (i32 value = 3; value < static_cast<i32>(proc::max_process_fds); ++value)
                if (current->set_descriptor(fd_t{value}, file)) {
                    fd = fd_t{value};
                    break;
                }
            if (fd.value < 0) {
                logs::error("aio capacity fail (park fd)");
                cleanup();
                return false;
            }

            u8 buffers[ASTIA_AIO_MAX_PARKED_OPERATIONS]{};
            for (u32 i = 0; i < ASTIA_AIO_MAX_PARKED_OPERATIONS; ++i) {
                astia_sqe* sqe = astia_sqe_slot(ring);
                if (sqe == nullptr) {
                    logs::error("aio capacity fail (park SQ)");
                    cleanup();
                    return false;
                }
                *sqe           = astia_sqe{};
                sqe->op        = OP_READ;
                sqe->fd        = fd.value;
                sqe->address   = reinterpret_cast<u64>(&buffers[i]);
                sqe->length    = 1;
                sqe->user_data = 0xC0DE9000 + i;
                astia_sq_publish(ring, 1);
                if (sys::io_submit(ring, 1) != 1 || astia_cq_ready(ring) != 0) {
                    logs::error("aio capacity fail (park fill)");
                    cleanup();
                    return false;
                }
            }
            astia_sqe* sqe = astia_sqe_slot(ring);
            if (sqe == nullptr) {
                logs::error("aio capacity fail (overflow SQ)");
                cleanup();
                return false;
            }
            *sqe           = astia_sqe{};
            sqe->op        = OP_READ;
            sqe->fd        = fd.value;
            sqe->address   = reinterpret_cast<u64>(&buffers[0]);
            sqe->length    = 1;
            sqe->user_data = 0xC0DE90FF;
            astia_sq_publish(ring, 1);
            if (sys::io_submit(ring, 1) != 1 || astia_cq_ready(ring) != 1) {
                logs::error("aio capacity fail (park overflow)");
                cleanup();
                return false;
            }
            astia_cqe* overflow = astia_cq_peek(ring);
            if (overflow == nullptr || overflow->user_data != 0xC0DE90FF ||
                overflow->result != -EBUSY) {
                logs::error("aio capacity fail (park error)");
                cleanup();
                return false;
            }
            astia_cq_consume(ring, 1);
            node.ready = true;
            sys::io_device_ready_context(&node);
            if (node.submit_calls < ASTIA_AIO_MAX_PARKED_OPERATIONS)
                return false;
            sys::io_process_deferred_ready();
            if (astia_cq_ready(ring) != ASTIA_AIO_MAX_PARKED_OPERATIONS ||
                node.submit_calls < 2 * ASTIA_AIO_MAX_PARKED_OPERATIONS) {
                logs::error("aio capacity fail (park resume)");
                cleanup();
                return false;
            }
            for (u32 i = 0; i < ASTIA_AIO_MAX_PARKED_OPERATIONS; ++i) {
                astia_cqe* completion = astia_cq_peek(ring);
                if (completion == nullptr || completion->user_data != 0xC0DE9000 + i ||
                    completion->result != 1 || buffers[i] != 0x6b) {
                    logs::error(
                        "aio capacity fail (park completion) i=%u ud=%llx want=%llx "
                        "res=%d buf=%x",
                        i, completion == nullptr ? 0ull : completion->user_data,
                        static_cast<unsigned long long>(0xC0DE9000 + i),
                        completion == nullptr ? -1 : completion->result, buffers[i]);
                    cleanup();
                    return false;
                }
                astia_cq_consume(ring, 1);
            }
            if (sys::io_submit(ring, 0) != 0) {
                logs::error("aio capacity fail (park reclaim)");
                cleanup();
                return false;
            }
            current->clear_descriptor(fd);
            fd   = fd_t{-1};
            file = {};
            if (sys::io_destroy(ring) != 0) {
                logs::error("aio capacity fail (park teardown)");
                return false;
            }
            logs::info("aio capacity boundaries ok");
            return true;
        }

        bool aio_dispatch_boundary_checks() {
            struct dispatch_probe : fs::file, fs::readable, fs::writable {
                bool ready        = false;
                bool error        = false;
                u32  submit_calls = 0;

                optional<fs::readable&> as_readable() override {
                    return *this;
                }
                optional<fs::writable&> as_writable() override {
                    return *this;
                }
                core::task<i64> readv(span<const fs::buffer> buffers, u64 offset,
                                      core::cltoken token) override {
                    return submit_readv(buffers, offset, token);
                }
                core::task<i64> writev(span<const fs::buffer> buffers, u64 offset,
                                       core::cltoken token) override {
                    return submit_writev(buffers, offset, token);
                }

                long submit(io::request& request) override {
                    ++submit_calls;
                    if (error)
                        return -EIO;
                    if (!ready)
                        return 0;
                    if (request.direction() == io::direction::input &&
                        request.length() != 0)
                        static_cast<u8*>(request.buffer())[0] = 0xa5;
                    return static_cast<long>(request.length());
                }
            } node;
            static const char path[] = "/dev/aio_dispatch_boundary";
            if (fs::register_device(path, mm::shptr<fs::file>::borrowed(&node)) < 0) {
                logs::error("aio dispatch fail (device registration)");
                return false;
            }

            astia_rings* ring    = nullptr;
            auto*        current = proc::current_process();
            fd_t         fd{-1};
            auto         fail = [&]() {
                logs::error("aio dispatch fail calls=%u cq=%u", node.submit_calls,
                            ring == nullptr ? 0 : astia_cq_ready(ring));
                if (current != nullptr && fd.value >= 0)
                    current->clear_descriptor(fd);
                if (ring != nullptr)
                    (void) sys::io_destroy(ring);
                return false;
            };
            if (current == nullptr || sys::io_setup(8, 0, &ring) < 0 || ring == nullptr)
                return fail();
            auto file = fs::open(path);
            if (!file)
                return fail();
            for (i32 value = 3; value < static_cast<i32>(proc::max_process_fds);
                 ++value) {
                if (current->set_descriptor(fd_t{value}, file)) {
                    fd = fd_t{value};
                    break;
                }
            }
            if (fd.value < 0)
                return fail();

            auto completion = [&](u32 op, i32 result, i32 op_fd, u64 address, u32 length,
                                  u64 user_data) {
                astia_sqe* sqe = astia_sqe_slot(ring);
                if (sqe == nullptr)
                    return false;
                *sqe           = astia_sqe{};
                sqe->op        = op;
                sqe->fd        = op_fd;
                sqe->address   = address;
                sqe->length    = length;
                sqe->user_data = user_data;
                astia_sq_publish(ring, 1);
                if (sys::io_submit(ring, 1) != 1 || astia_cq_ready(ring) != 1)
                    return false;
                astia_cqe* cqe = astia_cq_peek(ring);
                if (cqe == nullptr || cqe->user_data != user_data ||
                    cqe->result != result)
                    return false;
                astia_cq_consume(ring, 1);
                return sys::io_submit(ring, 0) == 0;
            };

            if (!completion(OP_NOP, 0, -1, 0, 0, 0xc0de80) ||
                !completion(0xffff, -ENOSYS, -1, 0, 0, 0xc0de81) ||
                !completion(OP_READ, -EBADF, -1, 0, 1, 0xc0de82) ||
                !completion(OP_READ, -EFAULT, fd.value, 0, 1, 0xc0de83) ||
                node.submit_calls != 0 ||
                !completion(OP_READV, -EFAULT, fd.value, 0, 1, 0xc0de84) ||
                node.submit_calls != 0)
                return fail();

            u8         buffer = 0;
            astia_sqe* sqe    = astia_sqe_slot(ring);
            if (sqe == nullptr)
                return fail();
            *sqe           = astia_sqe{};
            sqe->op        = OP_READ;
            sqe->fd        = fd.value;
            sqe->address   = reinterpret_cast<u64>(&buffer);
            sqe->length    = sizeof buffer;
            sqe->user_data = 0xc0de85;
            astia_sq_publish(ring, 1);
            if (sys::io_submit(ring, 1) != 1 || astia_cq_ready(ring) != 0 ||
                node.submit_calls != 1)
                return fail();

            node.ready = true;
            sys::io_device_ready_context(&node);
            if (astia_cq_ready(ring) != 0 || node.submit_calls != 1)
                return fail();
            sys::io_process_deferred_ready();
            if (astia_cq_ready(ring) != 1 || node.submit_calls != 2)
                return fail();
            astia_cqe* cqe = astia_cq_peek(ring);
            if (cqe == nullptr || cqe->user_data != 0xc0de85 || cqe->result != 1 ||
                buffer != 0xa5)
                return fail();
            astia_cq_consume(ring, 1);
            if (sys::io_submit(ring, 0) != 0)
                return fail();

            node.error = true;
            if (!completion(OP_READ, -EIO, fd.value, reinterpret_cast<u64>(&buffer),
                            sizeof buffer, 0xc0de86) ||
                node.submit_calls != 3)
                return fail();
            node.error = false;
            node.ready = false;
            sqe        = astia_sqe_slot(ring);
            if (sqe == nullptr)
                return fail();
            *sqe           = astia_sqe{};
            sqe->op        = OP_READ;
            sqe->fd        = fd.value;
            sqe->address   = reinterpret_cast<u64>(&buffer);
            sqe->length    = sizeof buffer;
            sqe->user_data = 0xc0de87;
            astia_sq_publish(ring, 1);
            if (sys::io_submit(ring, 1) != 1 || astia_cq_ready(ring) != 0)
                return fail();
            sqe = astia_sqe_slot(ring);
            if (sqe == nullptr)
                return fail();
            *sqe           = astia_sqe{};
            sqe->op        = OP_CANCEL;
            sqe->offset    = 0xc0de87;
            sqe->user_data = 0xc0de88;
            astia_sq_publish(ring, 1);
            if (sys::io_submit(ring, 1) != 1 || astia_cq_ready(ring) != 1)
                return fail();
            cqe = astia_cq_peek(ring);
            if (cqe == nullptr || cqe->user_data != 0xc0de88 || cqe->result != 0)
                return fail();
            astia_cq_consume(ring, 1);
            node.ready = true;
            sys::io_device_ready_context(&node);
            sys::io_process_deferred_ready();
            if (astia_cq_ready(ring) != 1)
                return fail();
            cqe = astia_cq_peek(ring);
            if (cqe == nullptr || cqe->user_data != 0xc0de87 || cqe->result != -ECANCELED)
                return fail();
            astia_cq_consume(ring, 1);
            if (sys::io_submit(ring, 0) != 0)
                return fail();

            current->clear_descriptor(fd);
            fd   = fd_t{-1};
            file = {};
            if (sys::io_destroy(ring) != 0)
                return false;
            ring = nullptr;
            logs::info("aio dispatch boundaries ok");
            return true;
        }

        bool cq_full_completion_checks() {
            static const char parked_path[] = "/dev/aio_cq_full_probe";
            constexpr u64     inline_tag    = 0xC0DE60;
            constexpr u64     parked_tag    = 0xC0DE61;
            if (fs::register_device(parked_path,
                                    mm::shptr<fs::file>::borrowed(&parked_node)) < 0) {
                logs::error("aio CQ fail (probe node)");
                return false;
            }

            astia_rings* ring = nullptr;
            if (sys::io_setup(8, 0, &ring) < 0 || ring == nullptr) {
                logs::error("aio CQ fail (ring)");
                return false;
            }
            auto  inline_file = fs::open("/dev/statx_probe");
            auto* current     = proc::current_process();
            fd_t  inline_fd{-1};
            if (!inline_file || current == nullptr) {
                logs::error("aio CQ fail (inline open)");
                (void) sys::io_destroy(ring);
                return false;
            }
            for (i32 fd = 3; fd < static_cast<i32>(proc::max_process_fds); ++fd) {
                if (current->set_descriptor(fd_t{fd}, inline_file)) {
                    inline_fd = fd_t{fd};
                    break;
                }
            }
            if (inline_fd.value < 0) {
                logs::error("aio CQ fail (inline fd)");
                (void) sys::io_destroy(ring);
                return false;
            }
            auto fill_cq = [&](u64 tag) {
                astia_cq_ring* cq = astia_rings_cq(ring);
                for (u32 i = astia_cq_ready(ring); i < cq->entries; ++i) {
                    astia_sqe* s = astia_sqe_slot(ring);
                    if (s == nullptr)
                        return false;
                    *s           = astia_sqe{};
                    s->op        = OP_NOP;
                    s->user_data = tag + i;
                    astia_sq_publish(ring, 1);
                    if (sys::io_submit(ring, 1) != 1)
                        return false;
                }
                return astia_cq_ready(ring) == cq->entries;
            };

            if (!fill_cq(0xF000)) {
                logs::error("aio CQ fail (fill inline)");
                (void) sys::io_destroy(ring);
                return false;
            }
            probe_node.submit_calls  = 0;
            u8         inline_buffer = 0;
            astia_sqe* s             = astia_sqe_slot(ring);
            if (s == nullptr) {
                logs::error("aio CQ fail (inline SQ slot)");
                current->clear_descriptor(inline_fd);
                (void) sys::io_destroy(ring);
                return false;
            }
            *s           = astia_sqe{};
            s->op        = OP_READ;
            s->fd        = inline_fd.value;
            s->address   = reinterpret_cast<u64>(&inline_buffer);
            s->length    = 1;
            s->user_data = inline_tag;
            astia_sq_publish(ring, 1);
            if (sys::io_submit(ring, 1) != 1 || probe_node.submit_calls < 1 ||
                astia_cq_ready(ring) != astia_rings_cq(ring)->entries) {
                logs::error("aio CQ fail (inline retained)");
                current->clear_descriptor(inline_fd);
                (void) sys::io_destroy(ring);
                return false;
            }
            astia_cq_consume(ring, 1);
            if (sys::io_submit(ring, 0) != 0 ||
                astia_cq_ready(ring) != astia_rings_cq(ring)->entries) {
                logs::error("aio CQ fail (inline retry)");
                current->clear_descriptor(inline_fd);
                (void) sys::io_destroy(ring);
                return false;
            }
            for (u32 i = 1; i < astia_rings_cq(ring)->entries; ++i)
                astia_cq_consume(ring, 1);
            astia_cqe* c = astia_cq_peek(ring);
            if (c == nullptr || c->user_data != inline_tag || c->result != 1) {
                logs::error("aio CQ fail (inline CQE)");
                current->clear_descriptor(inline_fd);
                (void) sys::io_destroy(ring);
                return false;
            }
            astia_cq_consume(ring, 1);
            if (sys::io_submit(ring, 0) != 0 || astia_cq_ready(ring) != 0) {
                logs::error("aio CQ fail (inline duplicate)");
                current->clear_descriptor(inline_fd);
                (void) sys::io_destroy(ring);
                return false;
            }
            current->clear_descriptor(inline_fd);
            inline_file = {};

            auto parked_file = fs::open(parked_path);
            if (!parked_file) {
                logs::error("aio CQ fail (parked open)");
                (void) sys::io_destroy(ring);
                return false;
            }
            fd_t parked_fd{-1};
            if (current == nullptr) {
                logs::error("aio CQ fail (parked process)");
                (void) sys::io_destroy(ring);
                return false;
            }
            for (i32 fd = 3; fd < static_cast<i32>(proc::max_process_fds); ++fd) {
                if (current->set_descriptor(fd_t{fd}, parked_file)) {
                    parked_fd = fd_t{fd};
                    break;
                }
            }
            if (parked_fd.value < 0) {
                logs::error("aio CQ fail (parked fd)");
                (void) sys::io_destroy(ring);
                return false;
            }
            u8 buffer                = 0;
            parked_node.ready        = false;
            parked_node.submit_calls = 0;
            s                        = astia_sqe_slot(ring);
            if (s == nullptr) {
                current->clear_descriptor(parked_fd);
                (void) sys::io_destroy(ring);
                return false;
            }
            *s           = astia_sqe{};
            s->op        = OP_READ;
            s->fd        = parked_fd.value;
            s->address   = reinterpret_cast<u64>(&buffer);
            s->length    = 1;
            s->user_data = parked_tag;
            astia_sq_publish(ring, 1);
            if (sys::io_submit(ring, 1) != 1 || astia_cq_ready(ring) != 0 ||
                parked_node.submit_calls != 1 || !fill_cq(0xF100)) {
                logs::error("aio CQ fail (parked setup)");
                current->clear_descriptor(parked_fd);
                (void) sys::io_destroy(ring);
                return false;
            }
            parked_node.ready = true;
            sys::io_device_ready_context(&parked_node);
            if (astia_cq_ready(ring) != astia_rings_cq(ring)->entries ||
                parked_node.submit_calls < 1) {
                logs::error("aio CQ fail (parked IRQ inline)");
                current->clear_descriptor(parked_fd);
                (void) sys::io_destroy(ring);
                return false;
            }
            sys::io_process_deferred_ready();
            if (astia_cq_ready(ring) != astia_rings_cq(ring)->entries ||
                parked_node.submit_calls < 2) {
                logs::error("aio CQ fail (parked retained)");
                current->clear_descriptor(parked_fd);
                (void) sys::io_destroy(ring);
                return false;
            }
            astia_cq_consume(ring, 1);
            if (sys::io_submit(ring, 0) != 0 ||
                astia_cq_ready(ring) != astia_rings_cq(ring)->entries ||
                parked_node.submit_calls < 2) {
                logs::error("aio CQ fail (parked retry)");
                current->clear_descriptor(parked_fd);
                (void) sys::io_destroy(ring);
                return false;
            }
            for (u32 i = 1; i < astia_rings_cq(ring)->entries; ++i)
                astia_cq_consume(ring, 1);
            c = astia_cq_peek(ring);
            if (c == nullptr || c->user_data != parked_tag || c->result != 1 ||
                buffer != 0x5a) {
                logs::error("aio CQ fail (parked CQE)");
                current->clear_descriptor(parked_fd);
                (void) sys::io_destroy(ring);
                return false;
            }
            astia_cq_consume(ring, 1);
            if (sys::io_submit(ring, 0) != 0 || astia_cq_ready(ring) != 0 ||
                parked_node.submit_calls < 2) {
                logs::error("aio CQ fail (parked duplicate)");
                current->clear_descriptor(parked_fd);
                (void) sys::io_destroy(ring);
                return false;
            }
            current->clear_descriptor(parked_fd);
            parked_file = {};
            (void) sys::io_destroy(ring);
            logs::info("aio CQ full completion ok");
            return true;
        }

        bool aio_cancel_checks() {
            struct cancel_probe : fs::file, fs::readable, fs::writable {
                long submit(io::request& request) override {
                    return ready ? static_cast<long>(request.length()) : 0;
                }

                optional<fs::readable&> as_readable() override {
                    return *this;
                }
                optional<fs::writable&> as_writable() override {
                    return *this;
                }
                core::task<i64> readv(span<const fs::buffer> buffers, u64 offset,
                                      core::cltoken token) override {
                    return submit_readv(buffers, offset, token);
                }
                core::task<i64> writev(span<const fs::buffer> buffers, u64 offset,
                                       core::cltoken token) override {
                    return submit_writev(buffers, offset, token);
                }

                bool ready = false;
            } node;
            static const char path[]     = "/dev/aio_cancel_probe";
            constexpr u64     target_tag = 0xC0DE70;
            constexpr u64     cancel_tag = 0xC0DE71;
            if (fs::register_device(path, mm::shptr<fs::file>::borrowed(&node)) < 0) {
                logs::error("aio cancel fail (probe node)");
                return false;
            }
            astia_rings* ring = nullptr;
            auto         fail = [&]() {
                if (ring != nullptr)
                    (void) sys::io_destroy(ring);
                return false;
            };
            if (sys::io_setup(8, 0, &ring) < 0 || ring == nullptr)
                return fail();
            auto  file    = fs::open(path);
            auto* current = proc::current_process();
            if (!file || current == nullptr)
                return fail();
            fd_t fd{-1};
            for (i32 value = 3; value < static_cast<i32>(proc::max_process_fds); ++value)
                if (current->set_descriptor(fd_t{value}, file)) {
                    fd = fd_t{value};
                    break;
                }
            if (fd.value < 0)
                return fail();
            u8         buffer = 0;
            astia_sqe* sqe    = astia_sqe_slot(ring);
            if (sqe == nullptr)
                return fail();
            *sqe           = astia_sqe{};
            sqe->op        = OP_READ;
            sqe->fd        = fd.value;
            sqe->address   = reinterpret_cast<u64>(&buffer);
            sqe->length    = sizeof buffer;
            sqe->user_data = target_tag;
            astia_sq_publish(ring, 1);
            if (sys::io_submit(ring, 1) != 1 || astia_cq_ready(ring) != 0)
                return fail();
            sqe = astia_sqe_slot(ring);
            if (sqe == nullptr)
                return fail();
            *sqe           = astia_sqe{};
            sqe->op        = OP_CANCEL;
            sqe->offset    = target_tag;
            sqe->user_data = cancel_tag;
            astia_sq_publish(ring, 1);
            if (sys::io_submit(ring, 1) != 1 || astia_cq_ready(ring) != 1)
                return fail();
            astia_cqe* first = astia_cq_peek(ring);
            if (first == nullptr || first->user_data != cancel_tag || first->result != 0)
                return fail();
            astia_cq_consume(ring, 1);
            node.ready = true;
            sys::io_device_ready_context(&node);
            if (sys::io_submit(ring, 0) != 0 || astia_cq_ready(ring) != 1)
                return fail();
            astia_cqe* second = astia_cq_peek(ring);
            if (second == nullptr || second->user_data != target_tag ||
                second->result != -ECANCELED)
                return fail();
            astia_cq_consume(ring, 1);
            current->clear_descriptor(fd);
            file = {};
            if (sys::io_destroy(ring) != 0)
                return false;
            logs::info("aio cancel ok");
            return true;
        }

        // OP_STATX on a ring: the success path (both address forms) and the
        // errors the VFS can actually produce for them.
        bool statx_checks() {
            static const char probe_path[]   = "/dev/statx_probe";
            static const char missing_path[] = "/dev/astia_no_such_node";
            constexpr u64     statx_tag      = 0xC0DE57;

            if (fs::register_device(probe_path,
                                    mm::shptr<fs::file>::borrowed(&probe_node)) < 0) {
                logs::error("statx fail (probe node)");
                return false;
            }

            astia_rings* ring = nullptr;
            if (sys::io_setup(8, 0, &ring) < 0 || ring == nullptr) {
                logs::error("statx fail (ring)");
                return false;
            }

            auto statx = [&](const char* path, i32 fd, astia_stat* out) -> long {
                astia_sqe* s = astia_sqe_slot(ring);
                if (s == nullptr) {
                    return -EBUSY;
                }
                *s           = astia_sqe{};
                s->op        = OP_STATX;
                s->fd        = fd;
                s->address   = path == nullptr ? 0 : reinterpret_cast<u64>(path);
                s->offset    = reinterpret_cast<u64>(out);
                s->length    = static_cast<u32>(sizeof(astia_stat));
                s->user_data = statx_tag;
                astia_sq_publish(ring, 1);
                if (sys::io_submit(ring, 1) < 0) {
                    return -EIO;
                }
                astia_cqe* c = astia_cq_peek(ring);
                if (c == nullptr || c->user_data != statx_tag) {
                    return -EIO;
                }
                const long result = c->result;
                astia_cq_consume(ring, 1);
                return result;
            };

            astia_stat st{};
            if (statx(probe_path, -1, &st) != 0 || st.size != 4096 || st.blocks != 8) {
                logs::error("statx fail (path form)");
                return false;
            }
            if (statx(missing_path, -1, &st) != -ENOENT) {
                logs::error("statx fail (missing path)");
                return false;
            }
            if (statx(nullptr, -1, &st) != -EBADF) {
                logs::error("statx fail (fstat without descriptor)");
                return false;
            }

            // An SQE is consumed when io_submit accepts it, not when its CQE
            // is reaped. Repeating the kick without publishing another SQE
            // must therefore produce neither another completion nor effect.
            probe_node.stat_calls = 0;
            astia_sqe* request    = astia_sqe_slot(ring);
            if (request == nullptr) {
                logs::error("io submit once fail (SQ slot)");
                return false;
            }
            *request           = astia_sqe{};
            request->op        = OP_STATX;
            request->fd        = -1;
            request->address   = reinterpret_cast<u64>(probe_path);
            request->offset    = reinterpret_cast<u64>(&st);
            request->length    = static_cast<u32>(sizeof(astia_stat));
            request->user_data = 0xC0DE58;
            astia_sq_publish(ring, 1);
            if (sys::io_submit(ring, 1) != 1 || probe_node.stat_calls != 1 ||
                astia_cq_ready(ring) != 1) {
                logs::error("io submit once fail (first kick)");
                return false;
            }
            if (sys::io_submit(ring, 1) != 0 || probe_node.stat_calls != 1 ||
                astia_cq_ready(ring) != 1) {
                logs::error("io submit once fail (repeat kick)");
                return false;
            }
            astia_cqe* completion = astia_cq_peek(ring);
            if (completion == nullptr || completion->user_data != 0xC0DE58) {
                logs::error("io submit once fail (completion)");
                return false;
            }
            astia_cq_consume(ring, 1);

            request = astia_sqe_slot(ring);
            if (request == nullptr) {
                logs::error("io submit once fail (reuse SQ slot)");
                return false;
            }
            *request           = astia_sqe{};
            request->op        = OP_STATX;
            request->fd        = -1;
            request->address   = reinterpret_cast<u64>(probe_path);
            request->offset    = reinterpret_cast<u64>(&st);
            request->length    = static_cast<u32>(sizeof(astia_stat));
            request->user_data = 0xC0DE59;
            astia_sq_publish(ring, 1);
            if (sys::io_submit(ring, 1) != 1 || probe_node.stat_calls != 2 ||
                astia_cq_ready(ring) != 1) {
                logs::error("io submit once fail (reuse kick)");
                return false;
            }
            completion = astia_cq_peek(ring);
            if (completion == nullptr || completion->user_data != 0xC0DE59) {
                logs::error("io submit once fail (reuse completion)");
                return false;
            }
            astia_cq_consume(ring, 1);

            (void) sys::io_destroy(ring);
            logs::info("statx ok");
            return true;
        }

        bool mount_lifecycle_checks() {
            if (fs::unmount_type("/") != -EBUSY ||
                fs::unmount_type("/missing") != -ENOENT) {
                logs::error("mount lifecycle fail (validation)");
                return false;
            }
            auto open = fs::open("/tmp/lifecycle", ASTIA_AIO_O_CREAT);
            if (!open || fs::unmount_type("/tmp") != -EBUSY) {
                logs::error("mount lifecycle fail (open handle)");
                return false;
            }
            open = {};
            if (fs::unmount_type("/tmp") != 0 ||
                fs::node_type("/tmp") != fs::directory_unknown ||
                fs::mount_type("tmpfs", "/tmp") != 0) {
                logs::error("mount lifecycle fail (teardown/remount)");
                return false;
            }
            if (fs::node_type("/tmp/lifecycle") != fs::directory_unknown) {
                logs::error("mount lifecycle fail (stale contents after remount)");
                return false;
            }
            if (fs::mount_type("tmpfs", "/tmp/.") != -EEXIST ||
                fs::mount_type("tmpfs", "/does-not-exist") != -ENOENT ||
                fs::mount_type("tmpfs", "tmpfs") != -EINVAL ||
                fs::make_directory(
                    "/tmp/nested",
                    fs::file_mode(fs::directory_dir, fs::file_permissions(0755))) != 0) {
                logs::error("mount lifecycle fail (normalization/nesting)");
                return false;
            }
            auto outer = fs::open("/tmp/nested/outer", ASTIA_AIO_O_CREAT);
            if (!outer) {
                logs::error("mount lifecycle fail (outer content)");
                return false;
            }
            outer = {};
            if (fs::mount_type("tmpfs", "/tmp/nested/") != 0) {
                logs::error("mount lifecycle fail (nested mount)");
                return false;
            }
            auto inner = fs::open("/tmp/nested/inner", ASTIA_AIO_O_CREAT);
            if (!inner) {
                logs::error("mount lifecycle fail (inner content)");
                return false;
            }
            inner = {};
            if (fs::unmount_type("/tmp/nested/.") != 0 ||
                fs::node_type("/tmp/nested/outer") != fs::directory_reg ||
                fs::node_type("/tmp/nested/inner") != fs::directory_unknown ||
                fs::unmount_type("/tmp") != 0) {
                logs::error("mount lifecycle fail (nested contents)");
                return false;
            }
            if (fs::mount_type("tmpfs", "/tmp") != 0) {
                logs::error("mount lifecycle fail (final remount)");
                return false;
            }
            if (fs::make_directory(
                    "/tmp/registry",
                    fs::file_mode(fs::directory_dir, fs::file_permissions(0755))) != 0) {
                logs::error("filesystem registry fail (test mountpoint)");
                return false;
            }

            registry_test_filesystem    test_type;
            fs::filesystem_registration registration;
            long                        factory_unregistration = 0;
            test_type.observe(&registration, &factory_unregistration);
            if (fs::register_type(&test_type, registration) != 0 ||
                !registration.valid()) {
                logs::error("filesystem registry fail (register)");
                return false;
            }
            fs::filesystem_registration duplicate;
            if (fs::register_type(&test_type, duplicate) != -EEXIST ||
                duplicate.valid()) {
                logs::error("filesystem registry fail (duplicate)");
                return false;
            }
            fs::filesystem_registration stale = registration;
            if (fs::mount_type("registry-test", "/tmp/registry") != 0 ||
                factory_unregistration != -EBUSY ||
                fs::unregister_type(registration) != -EBUSY) {
                logs::error("filesystem registry fail (factory or mounted busy)");
                return false;
            }
            if (fs::unmount_type("/tmp/registry") != 0 ||
                fs::unregister_type(registration) != 0 || registration.valid() ||
                fs::unregister_type(stale) != -ENOENT) {
                logs::error("filesystem registry fail (teardown or stale handle)");
                return false;
            }
            if (fs::node_type("/tmp") != fs::directory_dir ||
                fs::node_type("/dev") != fs::directory_dir) {
                logs::error("filesystem registry fail (built-in filesystem)");
                return false;
            }
            logs::info("filesystem registry ok");
            logs::info("mount lifecycle ok");
            return true;
        }

        // The DSO TLS reserve is address space until a probe touches it:
        // creating the arena must cost only the payload's frames, and the
        // whole reserve must invalidate without allocating.
        bool tls_arena_checks() {
            const usize     before = mm::kernel_frames().available_frames();
            proc::tls_arena layout{};
            if (!proc::measure_tls_arena(1, layout) || layout.reserve_pages == 0) {
                logs::error("tls arena geometry fail");
                return false;
            }
            auto* region = proc::create_tls_arena(layout);
            if (region == nullptr) {
                logs::error("tls arena allocation fail");
                return false;
            }
            const usize allocated    = before - mm::kernel_frames().available_frames();
            bool        reserve_lazy = true;
            for (usize page = 0; page < layout.reserve_pages; ++page)
                reserve_lazy = reserve_lazy && region->page_base(page) ==
                                                   mm::frame_allocator::invalid_frame;
            region->release();
            if (allocated != layout.payload_pages || !reserve_lazy ||
                mm::kernel_frames().available_frames() != before) {
                logs::error("tls arena reserve allocated");
                return false;
            }
            logs::info("tls arena reserve lazy ok");
            return true;
        }

    } // namespace

    bool run() {
        if (!scheduler_checks()) {
            return false;
        }
        if (!run_arch_checks()) {
            return false;
        }
        if (!join_checks()) {
            return false;
        }
        if (!preemption_checks()) {
            return false;
        }
        if (!clock_checks()) {
            return false;
        }
        if (!uname_checks()) {
            return false;
        }
        if (!aio_capacity_boundary_checks()) {
            return false;
        }
        if (!aio_dispatch_boundary_checks()) {
            return false;
        }
        if (!statx_checks()) {
            return false;
        }
        if (!cq_full_completion_checks()) {
            return false;
        }
        if (!aio_cancel_checks()) {
            return false;
        }
        if (!mount_lifecycle_checks()) {
            return false;
        }
        if (!tls_arena_checks()) {
            return false;
        }

        // The UART test owns its gate window and exercises the VFS/open
        // file/ring path in loopback mode.
        arch::cpu::interrupts();
        const bool ring_ok = dev::uart16550_ring_selftest();
        arch::cpu::nointerrupts();
        if (!ring_ok) {
            logs::error("uart16550 ring fail");
            return false;
        }
        logs::info("uart16550 ring ok");
        return true;
    }

} // namespace astia::test
