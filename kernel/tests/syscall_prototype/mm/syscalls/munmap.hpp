#pragma once

#include <syscall.hpp>

namespace astia::mm::syscall_prototype {
    struct MunmapHandler : sys::handler<MunmapHandler, static_cast<sys::syscall_id>(5)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
