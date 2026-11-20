#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct ThreadGetattrHandler
        : sys::handler<ThreadGetattrHandler, static_cast<sys::syscall_id>(63)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
