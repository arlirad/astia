#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct SetgidHandler : sys::handler<SetgidHandler, static_cast<sys::syscall_id>(38)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
