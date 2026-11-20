#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct SetsidHandler : sys::handler<SetsidHandler, static_cast<sys::syscall_id>(28)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
