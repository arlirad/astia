#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct ThreadCreate2Handler
        : sys::handler<ThreadCreate2Handler, static_cast<sys::syscall_id>(23)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
