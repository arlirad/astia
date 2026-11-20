#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct GetpgrpHandler
        : sys::handler<GetpgrpHandler, static_cast<sys::syscall_id>(26)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
