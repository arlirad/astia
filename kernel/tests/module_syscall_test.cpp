#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/core/resume_queue.hpp>

#include "../src/mod/module.hpp"
#include "../src/mod/syscall_abi.hpp"
#include "../src/proc/proc.hpp"
#include "../src/sys/syscall.hpp"
#include "support/test.hpp"

#include <cstddef>
#include <cstring>
#include <thread>

extern unsigned char fixture_dyn_blob[];
extern unsigned int  fixture_dyn_blob_len;
extern unsigned char fixture_exec_blob[];
extern unsigned int  fixture_exec_blob_len;
extern unsigned char module_lifecycle_no_fini_blob[];
extern unsigned int  module_lifecycle_no_fini_blob_len;

namespace astia::mod {
    module_error load_test_module(module_id& out, const module_load_options& options);
    const char*  last_open_path();
    void         set_open_path_error(module_error error);
} // namespace astia::mod

namespace {
    using astia::mod::module_error;
    using astia::mod::module_id;
    using astia::mod::module_info;
    using astia::mod::module_load_options;

    template <typename T>
    T run_task_sync(astia::core::task<T> task) {
        task.resume();

        while (!task.done()) {
            std::this_thread::yield();
            task.resume();
        }

        return *task.value();
    }

    void set_root(bool root) {
        (void) astia::proc::current_process()->seteuid(root ? 0u : 1u);
    }

    module_load_options named(const char* name) {
        module_load_options options{};
        astia::usize        i = 0;

        while (i + 1 < sizeof options.name && name[i] != '\0') {
            options.name[i] = name[i];

            ++i;
        }

        options.name[i] = '\0';

        return options;
    }

    astia::i64 call(astia::sys::syscall_id id, astia::u64 a0 = 0, astia::u64 a1 = 0,
                    astia::u64 a2 = 0, astia::u64 a3 = 0, astia::u64 a4 = 0,
                    astia::u64 a5 = 0) {
        astia::arch::syscall_return image{};

        return astia::sys::dispatch(id, a0, a1, a2, a3, a4, a5, image);
    }

    astia::i64 call_with_id(astia::sys::syscall_id syscall, module_id handle) {
        astia_module_id public_id{};

        if (!astia::mod::encode_public_id(handle, public_id))
            return -EINVAL;

        return call(syscall, reinterpret_cast<astia::u64>(&public_id));
    }

    astia::i64 call_modstat(module_id handle, astia_module_stat* out) {
        astia_module_id public_id{};

        if (!astia::mod::encode_public_id(handle, public_id))
            return -EINVAL;

        return call(astia::sys::syscall_id::modstat,
                    reinterpret_cast<astia::u64>(&public_id),
                    reinterpret_cast<astia::u64>(out));
    }

    void reset_registry() {
        set_root(true);

        while (true) {
            module_info        infos[astia::mod::max_modules]{};
            astia::usize       total   = 0;
            const astia::usize visible = astia::mod::snapshot(
                astia::span<module_info>(infos, astia::mod::max_modules), &total);
            if (visible == 0)
                return;

            bool progressed = false;

            for (astia::usize i = 0; i < visible; ++i) {
                const module_id id = infos[i].id;

                for (astia::usize d = 0; d < infos[i].dependency_count; ++d) {
                    module_id dependency{};

                    if (astia::mod::dependency_at(id, 0, dependency) == module_error::ok)
                        (void) astia::mod::remove_dependency(id, dependency);
                }

                if (infos[i].running)
                    (void) run_task_sync(astia::mod::exit(id));

                if (infos[i].resident)
                    (void) astia::mod::clear_resident_for_test(id);

                if (astia::mod::unload(id) == module_error::ok) {
                    progressed = true;

                    continue;
                }

                auto closing = astia::mod::close(id);

                closing.resume();

                while (!closing.done() && astia::core::drain_resumes() != 0) {
                    closing.resume();
                }

                if (!closing.done() && astia::mod::cancel_close(id) == module_error::ok) {
                    while (!closing.done() && astia::core::drain_resumes() != 0) {
                        closing.resume();
                    }
                }

                if (closing.done())
                    progressed = true;
            }

            if (!progressed)
                return;
        }
    }
} // namespace

ASTIA_TEST(module_syscall_ids_and_public_layout_are_canonical) {
    static_assert(static_cast<astia::u32>(astia::sys::syscall_id::dlmap) == 71);
    static_assert(static_cast<astia::u32>(astia::sys::syscall_id::dlunmap) == 72);
    static_assert(static_cast<astia::u32>(astia::sys::syscall_id::modopen) == 73);
    static_assert(static_cast<astia::u32>(astia::sys::syscall_id::modls) == 74);
    static_assert(static_cast<astia::u32>(astia::sys::syscall_id::modstat) == 75);
    static_assert(static_cast<astia::u32>(astia::sys::syscall_id::modclose) == 76);

    static_assert(SYS_DLMAP == 71 && SYS_DLUNMAP == 72);
    static_assert(SYS_MODOPEN == 73 && SYS_MODLS == 74);
    static_assert(SYS_MODSTAT == 75 && SYS_MODCLOSE == 76);

    static_assert(sizeof(astia_module_id) == 16);
    static_assert(offsetof(astia_module_id, generation) == 8);
    static_assert(sizeof(astia_module_dependency) == 24);
    static_assert(sizeof(astia_module_record) == 120);
    static_assert(offsetof(astia_module_record, name) == 56);
    static_assert(sizeof(astia_module_stat) == 312);
    static_assert(offsetof(astia_module_stat, dependencies) == 120);
    static_assert(sizeof(astia_module_list_info) == 24);
}

ASTIA_TEST(module_syscall_id_roundtrip_and_reserved_validation) {
    module_id       kernel_id{3, 42};
    astia_module_id public_id{};

    ASTIA_CHECK(astia::mod::encode_public_id(kernel_id, public_id));
    ASTIA_CHECK(public_id.slot == 3 && public_id.generation == 42 &&
                public_id.reserved == 0);

    module_id decoded{};

    ASTIA_CHECK(astia::mod::decode_public_id(public_id, decoded) == module_error::ok);
    ASTIA_CHECK(decoded == kernel_id);

    public_id.reserved = 1;

    ASTIA_CHECK(astia::mod::decode_public_id(public_id, decoded) ==
                module_error::invalid_argument);

    public_id.reserved = 0;
    public_id.slot     = ASTIA_MODULE_SLOT_NONE;

    ASTIA_CHECK(astia::mod::decode_public_id(public_id, decoded) ==
                module_error::not_found);
}

ASTIA_TEST(module_image_format_accepts_et_rel_and_rejects_executables) {
    const astia::span<const astia::u8> relocatable(module_lifecycle_no_fini_blob,
                                                   module_lifecycle_no_fini_blob_len);
    const astia::span<const astia::u8> dyn(fixture_dyn_blob, fixture_dyn_blob_len);
    const astia::span<const astia::u8> exec(fixture_exec_blob, fixture_exec_blob_len);

    ASTIA_CHECK(astia::elf::loader::kind_of(relocatable) ==
                astia::elf::loader::kind::relocatable);
    ASTIA_CHECK(astia::mod::validate_module_image_format(relocatable) ==
                module_error::ok);
    ASTIA_CHECK(astia::elf::loader::kind_of(dyn) == astia::elf::loader::kind::dyn);
    ASTIA_CHECK(astia::mod::validate_module_image_format(dyn) ==
                module_error::load_failed);
    ASTIA_CHECK(astia::elf::loader::kind_of(exec) ==
                astia::elf::loader::kind::executable);
    ASTIA_CHECK(astia::mod::validate_module_image_format(exec) ==
                module_error::load_failed);
    ASTIA_CHECK(astia::mod::module_errno(module_error::load_failed) == -ENOEXEC);

    const astia::u8 invalid[] = {0x7f, 'E', 'L', 'F'};

    ASTIA_CHECK(astia::mod::validate_module_image_format(astia::span<const astia::u8>(
                    invalid, sizeof invalid)) == module_error::load_failed);
}

ASTIA_TEST(module_close_waiter_allocation_failure_reports_no_memory) {
    reset_registry();
    set_root(true);

    module_id id{};

    ASTIA_CHECK(load_test_module(id, named("waiter-allocation")) == module_error::ok);
    ASTIA_CHECK(astia::mod::acquire(id) == module_error::ok);

    auto closing = astia::mod::close(id);

    astia_test::fail_next_allocation();
    closing.resume();
    ASTIA_CHECK(closing.done());
    ASTIA_CHECK(closing.value() != nullptr &&
                *closing.value() == module_error::no_memory);
    ASTIA_CHECK(astia::mod::module_errno(module_error::no_memory) == -ENOMEM);

    module_info info{};

    ASTIA_CHECK(astia::mod::lookup(id, info) == module_error::ok);
    ASTIA_CHECK(!info.quiescing);
    ASTIA_CHECK(astia::mod::release(id) == module_error::ok);
    ASTIA_CHECK(astia::mod::unload(id) == module_error::ok);
    reset_registry();
}

ASTIA_TEST(module_close_error_mapping_distinguishes_allocation_failure) {
    const astia::optional<module_error> allocation(astia::error_t::enomem);

    ASTIA_CHECK(astia::mod::module_close_errno(allocation) == -ENOMEM);

    const astia::optional<module_error> other(astia::error_t::eio);

    ASTIA_CHECK(astia::mod::module_close_errno(other) == -EIO);

    const astia::optional<module_error> resident(module_error::resident);

    ASTIA_CHECK(astia::mod::module_close_errno(resident) == -EPERM);

    const astia::optional<module_error> success(module_error::ok);

    ASTIA_CHECK(astia::mod::module_close_errno(success) == 0);
}

ASTIA_TEST(module_syscall_rejects_non_root_before_any_work) {
    reset_registry();
    set_root(false);
    ASTIA_CHECK(call(astia::sys::syscall_id::modopen) == -EPERM);
    ASTIA_CHECK(call(astia::sys::syscall_id::modls) == -EPERM);
    ASTIA_CHECK(call(astia::sys::syscall_id::modstat) == -EPERM);
    ASTIA_CHECK(call(astia::sys::syscall_id::modclose) == -EPERM);
    ASTIA_CHECK(astia::mod::count() == 0);
    set_root(true);
}

ASTIA_TEST(module_syscall_modopen_copies_out_stable_id_and_rejects_malformed) {
    reset_registry();
    set_root(true);
    astia::mod::set_open_path_error(module_error::ok);

    astia_module_id id{};

    char path[] = "/mods/sample";

    ASTIA_CHECK(call(astia::sys::syscall_id::modopen, reinterpret_cast<astia::u64>(path),
                     0, reinterpret_cast<astia::u64>(&id)) == 0);
    ASTIA_CHECK(id.slot != ASTIA_MODULE_SLOT_NONE && id.reserved == 0);
    ASTIA_CHECK(std::strcmp(astia::mod::last_open_path(), path) == 0);

    astia_module_stat stat{};

    ASTIA_CHECK(call(astia::sys::syscall_id::modstat, reinterpret_cast<astia::u64>(&id),
                     reinterpret_cast<astia::u64>(&stat)) == 0);
    ASTIA_CHECK(std::strcmp(stat.name, "sample") == 0);

    ASTIA_CHECK(call(astia::sys::syscall_id::modopen, reinterpret_cast<astia::u64>(path),
                     1, reinterpret_cast<astia::u64>(&id)) == -EINVAL);
    ASTIA_CHECK(call(astia::sys::syscall_id::modopen, 0, 0,
                     reinterpret_cast<astia::u64>(&id)) == -EFAULT);
    ASTIA_CHECK(call(astia::sys::syscall_id::modopen, reinterpret_cast<astia::u64>(path),
                     0, 0) == -EFAULT);

    char unterminated[astia::mod::max_module_name * 2];

    std::memset(unterminated, 'a', sizeof unterminated);
    ASTIA_CHECK(call(astia::sys::syscall_id::modopen,
                     reinterpret_cast<astia::u64>(unterminated), 0,
                     reinterpret_cast<astia::u64>(&id)) == -EFAULT);

    astia::mod::set_open_path_error(module_error::load_failed);

    const astia::usize before = astia::mod::count();

    ASTIA_CHECK(call(astia::sys::syscall_id::modopen, reinterpret_cast<astia::u64>(path),
                     0, reinterpret_cast<astia::u64>(&id)) == -ENOEXEC);
    ASTIA_CHECK(astia::mod::count() == before);

    astia::mod::set_open_path_error(module_error::stale_id);
    ASTIA_CHECK(call(astia::sys::syscall_id::modopen, reinterpret_cast<astia::u64>(path),
                     0, reinterpret_cast<astia::u64>(&id)) == -ENOENT);
    astia::mod::set_open_path_error(module_error::ok);
    reset_registry();
}

ASTIA_TEST(module_syscall_modls_reports_bounded_capacity_and_total) {
    reset_registry();
    set_root(true);

    module_id first{};
    module_id second{};

    ASTIA_CHECK(load_test_module(first, named("first")) == module_error::ok);
    ASTIA_CHECK(load_test_module(second, named("second")) == module_error::ok);

    astia_module_list_info info{};

    ASTIA_CHECK(call(astia::sys::syscall_id::modls, 0, 0,
                     reinterpret_cast<astia::u64>(&info)) == 0);
    ASTIA_CHECK(info.count == 0 && info.total == 2 && info.capacity == 0);

    astia_module_record records[ASTIA_MODULE_LIST_MAX]{};

    std::memset(&info, 0, sizeof info);
    ASTIA_CHECK(call(astia::sys::syscall_id::modls, reinterpret_cast<astia::u64>(records),
                     1, reinterpret_cast<astia::u64>(&info)) == 0);
    ASTIA_CHECK(info.count == 1 && info.total == 2 && info.capacity == 1);
    ASTIA_CHECK(records[0].size == sizeof(astia_module_record));
    ASTIA_CHECK(records[0].version == ASTIA_MODULE_INFO_VERSION);
    ASTIA_CHECK(records[0].id.slot != ASTIA_MODULE_SLOT_NONE);

    std::memset(records, 0, sizeof records);
    ASTIA_CHECK(call(astia::sys::syscall_id::modls, reinterpret_cast<astia::u64>(records),
                     ASTIA_MODULE_LIST_MAX, reinterpret_cast<astia::u64>(&info)) == 0);
    ASTIA_CHECK(info.count == 2 && info.total == 2);

    bool saw_first  = false;
    bool saw_second = false;

    for (astia::u32 i = 0; i < info.count; ++i) {
        if (std::strcmp(records[i].name, "first") == 0)
            saw_first = true;

        if (std::strcmp(records[i].name, "second") == 0)
            saw_second = true;
    }

    ASTIA_CHECK(saw_first && saw_second);

    ASTIA_CHECK(call(astia::sys::syscall_id::modls, reinterpret_cast<astia::u64>(records),
                     ASTIA_MODULE_LIST_MAX + 1,
                     reinterpret_cast<astia::u64>(&info)) == -EINVAL);
    ASTIA_CHECK(call(astia::sys::syscall_id::modls, 0, 1,
                     reinterpret_cast<astia::u64>(&info)) == -EFAULT);
    ASTIA_CHECK(call(astia::sys::syscall_id::modls, reinterpret_cast<astia::u64>(records),
                     1, 0) == -EFAULT);
    reset_registry();
}

ASTIA_TEST(module_syscall_modstat_exposes_state_dependencies_and_residency) {
    reset_registry();
    set_root(true);

    module_id dependent{};
    module_id provider{};

    ASTIA_CHECK(load_test_module(dependent, named("dependent")) == module_error::ok);
    ASTIA_CHECK(load_test_module(provider, named("provider")) == module_error::ok);
    ASTIA_CHECK(astia::mod::add_dependency(dependent, provider) == module_error::ok);

    astia_module_stat stat{};

    ASTIA_CHECK(call_modstat(dependent, &stat) == 0);
    ASTIA_CHECK(stat.dependency_count == 1 && stat.dependencies_written == 1);
    ASTIA_CHECK(stat.dependencies[0].id.slot == provider.slot);
    ASTIA_CHECK(stat.dependencies[0].id.generation == provider.generation);

    ASTIA_CHECK(call_modstat(provider, &stat) == 0);
    ASTIA_CHECK(stat.dependent_count == 1);
    ASTIA_CHECK((stat.flags & ASTIA_MODULE_FLAG_UNLOADABLE) == 0);

    ASTIA_CHECK(run_task_sync(astia::mod::run(provider)) == module_error::ok);
    ASTIA_CHECK(call_modstat(provider, &stat) == 0);
    ASTIA_CHECK(stat.state == ASTIA_MODULE_STATE_RUNNING);
    ASTIA_CHECK((stat.flags & ASTIA_MODULE_FLAG_RUNNING) != 0);

    module_load_options resident = named("resident");

    resident.resident = true;

    module_id resident_id{};

    ASTIA_CHECK(load_test_module(resident_id, resident) == module_error::ok);
    ASTIA_CHECK(call_modstat(resident_id, &stat) == 0);
    ASTIA_CHECK((stat.flags & ASTIA_MODULE_FLAG_RESIDENT) != 0);
    ASTIA_CHECK((stat.flags & ASTIA_MODULE_FLAG_UNLOADABLE) == 0);
    reset_registry();
}

ASTIA_TEST(module_syscall_modclose_rejects_running_resident_and_waits_drain) {
    reset_registry();
    set_root(true);

    module_id running{};

    ASTIA_CHECK(load_test_module(running, named("running")) == module_error::ok);
    ASTIA_CHECK(run_task_sync(astia::mod::run(running)) == module_error::ok);
    ASTIA_CHECK(call_with_id(astia::sys::syscall_id::modclose, running) == -EBUSY);

    module_load_options resident = named("resident-close");

    resident.resident = true;

    module_id resident_id{};

    ASTIA_CHECK(load_test_module(resident_id, resident) == module_error::ok);
    ASTIA_CHECK(call_with_id(astia::sys::syscall_id::modclose, resident_id) == -EPERM);

    module_id provider{};

    ASTIA_CHECK(load_test_module(provider, named("provider-close")) == module_error::ok);
    ASTIA_CHECK(astia::mod::acquire(provider) == module_error::ok);
    ASTIA_CHECK(call_with_id(astia::sys::syscall_id::modclose, provider) == -EAGAIN);

    module_info pending{};

    ASTIA_CHECK(astia::mod::lookup(provider, pending) == module_error::ok);
    ASTIA_CHECK(!pending.quiescing);
    ASTIA_CHECK(astia::mod::release(provider) == module_error::ok);
    ASTIA_CHECK(astia::mod::acquire(provider) == module_error::ok);

    auto closing = astia::mod::close(provider);

    closing.resume();
    ASTIA_CHECK(!closing.done());
    ASTIA_CHECK(astia::mod::lookup(provider, pending) == module_error::ok);
    ASTIA_CHECK(pending.quiescing);
    ASTIA_CHECK(astia::mod::acquire(provider) == module_error::would_block);
    ASTIA_CHECK(call_with_id(astia::sys::syscall_id::modclose, provider) == -EBUSY);
    ASTIA_CHECK(astia::mod::release(provider) == module_error::ok);
    ASTIA_CHECK(!closing.done());
    ASTIA_CHECK(astia::core::drain_resumes() == 1);
    ASTIA_CHECK(closing.done());
    ASTIA_CHECK(closing.value() != nullptr && *closing.value() == module_error::ok);
    reset_registry();
}

ASTIA_TEST(module_syscall_modclose_rejects_dependents_and_stale_ids) {
    reset_registry();
    set_root(true);

    module_id dependent{};
    module_id provider{};

    ASTIA_CHECK(load_test_module(dependent, named("dependent-close")) ==
                module_error::ok);
    ASTIA_CHECK(load_test_module(provider, named("provider-blocked")) ==
                module_error::ok);
    ASTIA_CHECK(astia::mod::add_dependency(dependent, provider) == module_error::ok);
    ASTIA_CHECK(call_with_id(astia::sys::syscall_id::modclose, provider) == -EAGAIN);

    module_info pending{};

    ASTIA_CHECK(astia::mod::lookup(provider, pending) == module_error::ok);
    ASTIA_CHECK(pending.dependent_count == 1);
    reset_registry();
}

ASTIA_TEST(module_syscall_slot_reuse_keeps_old_ids_stale) {
    reset_registry();
    set_root(true);

    module_id first{};

    ASTIA_CHECK(load_test_module(first, named("first-slot")) == module_error::ok);
    ASTIA_CHECK(call_with_id(astia::sys::syscall_id::modclose, first) == 0);

    astia_module_stat stat{};

    ASTIA_CHECK(call_modstat(first, &stat) == -ENOENT);

    module_id second{};

    ASTIA_CHECK(load_test_module(second, named("second-slot")) == module_error::ok);
    ASTIA_CHECK(second.slot == first.slot);
    ASTIA_CHECK(second.generation != first.generation);
    ASTIA_CHECK(call_modstat(first, &stat) == -ENOENT);
    ASTIA_CHECK(call_modstat(second, &stat) == 0);
    reset_registry();
}
