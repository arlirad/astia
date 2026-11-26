#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include "../module.hpp"
#include "../syscall_abi.hpp"
#include "proc/proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::mod::syscalls {
    struct modls
        : sys::syscall_handler<modls, sys::syscall_id::modls, sys::syscall_mode::sync> {
        static i64 invoke(sys::user_ptr<astia_module_record> records, u32 capacity,
                          sys::user_ptr<astia_module_list_info> output) {
            auto* process = proc::current_process();
            if (process == nullptr)
                return -ESRCH;

            if (process->euid() != 0)
                return -EPERM;

            if (capacity > ASTIA_MODULE_LIST_MAX)
                return -EINVAL;

            auto info = output.validate();
            if (!info)
                return -EFAULT;

            optional<sys::validated_user_ptr<astia_module_record>> range;

            if (capacity != 0) {
                range = records.validate(capacity);
                if (!range)
                    return -EFAULT;
            }

            module_info infos[ASTIA_MODULE_LIST_MAX]{};

            usize       total   = 0;
            const usize written = snapshot(span<module_info>(infos, capacity), &total);

            astia_module_record result[ASTIA_MODULE_LIST_MAX]{};

            for (usize i = 0; i < written; ++i) {
                fill_module_record(infos[i], result[i]);
            }

            astia_module_list_info list{};

            list.size     = sizeof(astia_module_list_info);
            list.version  = ASTIA_MODULE_LIST_VERSION;
            list.count    = static_cast<u32>(written);
            list.total    = static_cast<u32>(total);
            list.capacity = capacity;

            u8 info_bytes[sizeof list]{};

            __builtin_memcpy(info_bytes, &list, sizeof list);

            if (!sys::copy_out(info.value.bytes(), info_bytes))
                return -EFAULT;

            if (written != 0 &&
                !sys::copy_out(span<u8>::bytes(reinterpret_cast<u64>(range.value.get()),
                                               written * sizeof(astia_module_record)),
                               result))
                return -EFAULT;

            return 0;
        }
    };
} // namespace astia::mod::syscalls

#if defined(ASTIA_MODULE_HOST_TEST)
namespace {
    struct modls_host_registration {
        modls_host_registration() {
            (void) astia::sys::register_descriptor(
                &astia::sys::syscall_handler<astia::mod::syscalls::modls,
                                             astia::sys::syscall_id::modls,
                                             astia::sys::syscall_mode::sync>::descriptor);
        }
    } modls_host_registration_instance;
} // namespace
#endif
