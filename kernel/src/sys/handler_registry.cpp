#include "handler_registry.hpp"

#include <astia/api/errno.h>

namespace astia::sys {

    namespace {
        template <usize Count>
        bool dispatch_from(const syscall_descriptor* const (&descriptors)[Count],
                           syscall_context& context, i64& result) {
            for (const auto* descriptor : descriptors) {
                if (descriptor->id != context.id)
                    continue;
                core::completion<i64> completion;
                descriptor->invoke(context, completion);
                if (!completion.done())
                    (void) completion.cancel();
                const auto value = completion.await_resume();
                result           = value ? value.value : -EINTR;
                return true;
            }
            return false;
        }
    } // namespace

    bool production_dispatch(syscall_context& context, i64& result) {
        return dispatch_from(production::descriptors, context, result) ||
               dispatch_from(identity::descriptors, context, result);
    }
} // namespace astia::sys
