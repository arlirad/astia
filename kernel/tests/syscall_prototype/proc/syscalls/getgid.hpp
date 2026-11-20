#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct GetgidHandler : sys::handler<GetgidHandler, static_cast<sys::syscall_id>(34)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
