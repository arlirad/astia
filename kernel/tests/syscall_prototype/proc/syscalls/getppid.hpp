#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct GetppidHandler
        : sys::handler<GetppidHandler, static_cast<sys::syscall_id>(53)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
