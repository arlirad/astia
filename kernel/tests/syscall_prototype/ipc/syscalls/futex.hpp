#pragma once

#include <syscall.hpp>

namespace astia::ipc::syscall_prototype {
    struct FutexHandler : sys::handler<FutexHandler, static_cast<sys::syscall_id>(12)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
