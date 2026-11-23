#include "handler_registry.hpp"

#include <astia/api/errno.h>

namespace astia::sys {

    namespace {
        template <usize Count>
        bool dispatch_from(const syscall_descriptor* const (&descriptors)[Count],
                           syscall_id id, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5,
                           arch::syscall_return& image, i64& result) {
            for (const auto* descriptor : descriptors) {
                if (descriptor->id != id)
                    continue;
                core::completion<i64> completion;
                descriptor->invoke(a0, a1, a2, a3, a4, a5, image, completion);
                if (!completion.done())
                    (void) completion.cancel();
                const auto value = completion.await_resume();
                result           = value ? value.value : -EINTR;
                return true;
            }
            return false;
        }
    } // namespace

    bool production_dispatch(syscall_id id, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4,
                             u64 a5, arch::syscall_return& image, i64& result) {
        return dispatch_from(production::descriptors, id, a0, a1, a2, a3, a4, a5, image,
                             result) ||
               dispatch_from(identity::descriptors, id, a0, a1, a2, a3, a4, a5, image,
                             result);
    }
} // namespace astia::sys
