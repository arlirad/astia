#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct SpawnveHandler
        : sys::handler<SpawnveHandler, static_cast<sys::syscall_id>(14)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
