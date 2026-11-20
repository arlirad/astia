#pragma once

#include <syscall.hpp>

namespace astia::fs::syscall_prototype {
    struct ShmUnlinkHandler
        : sys::handler<ShmUnlinkHandler, static_cast<sys::syscall_id>(68)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
