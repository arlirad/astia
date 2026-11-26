#pragma once

#include <astia/core/task.hpp>

namespace astia_test {
    /// Drive a task once and require it to complete without suspending.
    /// Host tests have no scheduler; a suspension is a test failure.
    template <typename T>
    bool run_task(astia::core::task<T> task, T& out) {
        if (!task.valid())
            return false;

        task.resume();

        if (!task.done() || task.failed() || task.value() == nullptr)
            return false;

        out = *task.value();

        return true;
    }

    inline bool run_task(astia::core::task<void> task) {
        if (!task.valid())
            return false;

        task.resume();

        return task.done() && !task.failed();
    }
} // namespace astia_test
