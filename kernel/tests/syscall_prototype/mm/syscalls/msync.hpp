#pragma once

#include <syscall.hpp>

namespace astia::mm::syscall_prototype {
    struct MsyncHandler : sys::handler<MsyncHandler, static_cast<sys::syscall_id>(64)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
