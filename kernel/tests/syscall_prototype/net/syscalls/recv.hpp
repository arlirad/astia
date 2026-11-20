#pragma once

#include <syscall.hpp>

namespace astia::net::syscall_prototype {
    struct RecvHandler : sys::handler<RecvHandler, static_cast<sys::syscall_id>(75)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
