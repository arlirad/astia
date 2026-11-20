#pragma once

#include <syscall.hpp>

namespace astia::fs::syscall_prototype {
    struct IoRegisterHandler
        : sys::handler<IoRegisterHandler, static_cast<sys::syscall_id>(3)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
