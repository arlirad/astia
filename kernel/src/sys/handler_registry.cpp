#include "handler_registry.hpp"

#include <astia/api/errno.h>
namespace astia::sys {
    bool production_dispatch(syscall_context& context, i64& result) {
        for (const auto& descriptor : production::descriptors) {
            if (descriptor.id != context.id)
                continue;
            core::completion<i64> completion;
            descriptor.invoke(context, completion);
            if (!completion.done())
                (void) completion.cancel();
            const auto value = completion.await_resume();
            result           = value ? value.value : -EINTR;
            return true;
        }
        return false;
    }
} // namespace astia::sys
