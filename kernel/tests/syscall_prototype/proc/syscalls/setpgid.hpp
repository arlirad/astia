#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct SetpgidHandler
        : sys::handler<SetpgidHandler, static_cast<sys::syscall_id>(27)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
