#include <astia/fs.hpp>
#include <astia/io/request_queue.hpp>
#include <astia/mapping.hpp>

namespace astia::io {

    bool request_queue::park(request& request) {
        core::spinlock_guard guard(_lock);
        request._queue_next = nullptr;
        if (_tail == nullptr)
            _head = &request;
        else
            _tail->_queue_next = &request;
        _tail = &request;
        return true;
    }

    void request_queue::cancel(request& request) {
        core::spinlock_guard guard(_lock);
        io::request*         previous = nullptr;
        for (io::request* entry = _head; entry != nullptr; entry = entry->_queue_next) {
            if (entry != &request) {
                previous = entry;
                continue;
            }
            if (previous == nullptr)
                _head = entry->_queue_next;
            else
                previous->_queue_next = entry->_queue_next;
            if (_tail == entry)
                _tail = previous;
            entry->_queue_next = nullptr;
            return;
        }
    }

    bool request_queue::drive(request& request) {
        if (request.cancellation_requested()) {
            request.acknowledge();
            if (request.completion() != nullptr)
                request.completion()->fail_deferred(error_t::ecanceled);
            return true;
        }
        if (!request.begin_device()) {
            if (request.complete()) {
                if (request.completion() != nullptr)
                    request.completion()->complete_deferred(0);
                return true;
            }
            return false;
        }

        const long result = _owner.submit(request);
        if (result == 0 && !request.complete()) {
            request.end_device();
            return false;
        }

        request.acknowledge();
        request.end_device();
        if (request.completion() != nullptr) {
            if (result < 0)
                request.completion()->fail_deferred(error_from_errno(-result));
            else
                request.completion()->complete_deferred(result);
        }
        return true;
    }

    bool request_queue::empty() {
        core::spinlock_guard guard(_lock);
        return _head == nullptr;
    }

    void request_queue::service() {
        for (;;) {
            io::request* entry = nullptr;
            {
                core::spinlock_guard guard(_lock);
                entry = _head;
                if (entry == nullptr)
                    return;
                _head = entry->_queue_next;
                if (_head == nullptr)
                    _tail = nullptr;
                entry->_queue_next = nullptr;
            }

            if (drive(*entry))
                continue;

            core::spinlock_guard guard(_lock);
            if (_tail == nullptr)
                _head = entry;
            else
                _tail->_queue_next = entry;
            _tail = entry;
        }
    }

} // namespace astia::io
