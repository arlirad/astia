#pragma once

#include <astia/core/spinlock.hpp>
#include <astia/io.hpp>

namespace astia::fs {
    class file;
}

namespace astia::io {

    /// A file/device-owned queue of parked requests.  submit() that cannot
    /// finish appends here; the file's readiness path re-drives the queue in
    /// task context and completes each request's existing completion.
    class request_queue {
        public:
        constexpr explicit request_queue(fs::file& owner) : _owner(owner) {}
        request_queue(const request_queue&)            = delete;
        request_queue& operator=(const request_queue&) = delete;

        bool park(request& request);
        void cancel(request& request);
        void service();
        bool empty();

        private:
        bool drive(request& request);

        fs::file&      _owner;
        request*       _head = nullptr;
        request*       _tail = nullptr;
        core::spinlock _lock;
    };

} // namespace astia::io
