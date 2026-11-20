#pragma once

#include <syscall.hpp>

namespace astia::net::syscall_prototype {
    struct ListenHandler : sys::handler<ListenHandler, static_cast<sys::syscall_id>(74)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
