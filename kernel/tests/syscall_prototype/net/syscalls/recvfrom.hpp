#pragma once

#include <syscall.hpp>

namespace astia::net::syscall_prototype {
    struct RecvfromHandler
        : sys::handler<RecvfromHandler, static_cast<sys::syscall_id>(76)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
