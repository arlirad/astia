#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct GetgroupsHandler
        : sys::handler<GetgroupsHandler, static_cast<sys::syscall_id>(42)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
