#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct GetpidHandler : sys::handler<GetpidHandler, static_cast<sys::syscall_id>(10)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
