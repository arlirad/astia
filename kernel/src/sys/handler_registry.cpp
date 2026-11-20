#include "handler_registry.hpp"

#include <astia/api/errno.h>

namespace astia::sys {
    namespace {
        template <typename List>
        struct dispatch_list;
        template <typename... handlers>
        struct dispatch_list<type_list<handlers...>> {
            static bool dispatch(syscall_id id, syscall_context& context,
                                 core::completion<i64>& completion) {
                return dispatch_async<handlers...>(id, context, completion);
            }
        };
    }

    bool production_dispatch(syscall_context& context, i64& result) {
        core::completion<i64> completion;
        const bool            matched = dispatch_list<production::handler_set>::dispatch(
            context.id, context, completion);
        if (!matched)
            return false;
        if (!completion.done())
            (void) completion.cancel();
        const auto value = completion.await_resume();
        result           = value ? value.value : -EINTR;
        return true;
    }

} // namespace astia::sys
