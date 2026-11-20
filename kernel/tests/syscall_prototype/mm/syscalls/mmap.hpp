#pragma once

#include <syscall.hpp>

namespace astia::mm::syscall_prototype {
    struct MmapHandler : sys::handler<MmapHandler, static_cast<sys::syscall_id>(4)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
