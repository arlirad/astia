#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct SetegidHandler
        : sys::handler<SetegidHandler, static_cast<sys::syscall_id>(39)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
