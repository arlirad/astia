#pragma once

#include <astia/common.hpp>

#include <coroutine>

namespace astia::core {

    using coroutine_destroy_observer = void (*)(void* context);

    bool resume_begin(std::coroutine_handle<> handle) noexcept;
    void resume_end(std::coroutine_handle<> handle) noexcept;
    bool resume_now(std::coroutine_handle<> handle) noexcept;

    bool destroy_coroutine(std::coroutine_handle<>    handle,
                           coroutine_destroy_observer observer = nullptr,
                           void*                      context  = nullptr) noexcept;

    u32 coroutine_lifecycle_capacity() noexcept;
    u32 coroutine_lifecycle_inflight() noexcept;
    u32 coroutine_lifecycle_pending() noexcept;

} // namespace astia::core
