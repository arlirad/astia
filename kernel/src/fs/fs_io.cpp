#include <astia/fs.hpp>
#include <astia/mapping.hpp>

#include "../sys/aio.hpp"

namespace astia::fs {
    namespace {
        struct pending_operation {
            io::operation* operation = nullptr;
            file*          node      = nullptr;

            pending_operation(io::operation* value, file* owner)
                : operation(value), node(owner) {}

            pending_operation(const pending_operation&)            = delete;
            pending_operation& operator=(const pending_operation&) = delete;

            ~pending_operation() {
                if (operation == nullptr)
                    return;

                if (node != nullptr)
                    node->pending().cancel(operation->request());

                operation->release_owner();
            }
        };

        void wake_file(void* context) {
            if (context != nullptr)
                sys::io_device_ready_context(static_cast<file*>(context));
        }

        bool buffer_bytes(span<const buffer> buffers, u64& total) {
            total = 0;

            for (usize i = 0; i < buffers.size(); ++i) {
                const buffer& entry = buffers[i];

                if (entry.length > static_cast<usize>(~u32{0}))
                    return false;

                if (total > ~u64{0} - entry.length)
                    return false;

                total += entry.length;
            }

            return true;
        }

        /// Build one device-facing request per buffer and submit it to the
        /// node.  A parked request is owned by the device-side pending queue;
        /// its completion resumes this coroutine when the device has data.
        core::task<i64> submit_vector(file* node, span<const buffer> buffers, u64 offset,
                                      io::direction direction, bool nonblock,
                                      core::cltoken token) {
            i64 total = 0;

            for (usize i = 0; i < buffers.size(); ++i) {
                const buffer& entry = buffers[i];

                if (entry.length == 0)
                    continue;

                if (token.canceled()) {
                    if (total != 0)
                        co_return total;

                    co_return error_t::ecanceled;
                }

                if (node->pending().full()) {
                    if (total != 0)
                        co_return total;

                    co_return error_t::ebusy;
                }

                auto completion =
                    mm::uptr<core::completion<i64>>(new core::completion<i64>());
                if (!completion) {
                    if (total != 0)
                        co_return total;

                    co_return error_t::enomem;
                }

                auto* operation = new io::operation(
                    entry.data, static_cast<u32>(entry.length), offset + total, 0,
                    direction, static_cast<mm::uptr<core::completion<i64>>&&>(completion),
                    token);
                if (operation == nullptr) {
                    if (total != 0)
                        co_return total;

                    co_return error_t::enomem;
                }

                operation->heap_owned(true);

                pending_operation pending(operation, node);

                auto& request = operation->request();

                request.nonblock(nonblock);
                request.wake_handler_set(wake_file, node);

                if (!request.park_device()) {
                    if (total != 0)
                        co_return total;

                    co_return error_t::eagain;
                }

                if (!request.begin_device()) {
                    if (total != 0)
                        co_return total;

                    co_return error_t::eagain;
                }

                long submitted = node->submit(request);

                if (submitted == 0 && !request.complete()) {
                    request.end_device();

                    if (!node->pending().park(request)) {
                        request.acknowledge();

                        if (total != 0)
                            co_return total;

                        co_return error_t::ebusy;
                    }

                    // A poll-driven backend needs the queue re-driven even
                    // when it cannot raise an interrupt; keep the file in the
                    // deferred-ready set until the queue drains.
                    sys::io_device_ready_context(node);

                    auto result = co_await *request.completion();

                    request.acknowledge();

                    if (!result) {
                        if (total != 0)
                            co_return total;

                        co_return result.error;
                    }

                    submitted = static_cast<long>(result.value);
                }
                else {
                    request.acknowledge();
                    request.end_device();
                }

                if (submitted < 0) {
                    if (total != 0)
                        co_return total;

                    co_return error_from_errno(-submitted);
                }

                if (token.canceled()) {
                    if (total != 0)
                        co_return total;

                    co_return error_t::ecanceled;
                }

                total += submitted;

                if (submitted < static_cast<long>(entry.length))
                    break;
            }

            co_return total;
        }
    } // namespace

    core::task<i64> file::submit_readv(span<const buffer> buffers, u64 offset,
                                       core::cltoken token) {
        return submit_vector(this, buffers, offset, io::direction::input, false, token);
    }

    core::task<i64> file::submit_writev(span<const buffer> buffers, u64 offset,
                                        core::cltoken token, bool nonblock) {
        return submit_vector(this, buffers, offset, io::direction::output, nonblock,
                             token);
    }

    core::task<i64> open_file::readv(span<const buffer> buffers, u64 offset,
                                     core::cltoken token) {
        u64 sized = 0;

        if (!buffer_bytes(buffers, sized))
            co_return error_t::erange;

        if (sized == 0)
            co_return i64{0};

        if (!_parent)
            co_return error_t::ebadf;

        auto contract = _parent->as_readable();
        if (!contract)
            co_return error_t::ebadf;

        const bool current = offset == offset_current;
        const u64  actual  = current ? current_position() : offset;

        auto result = co_await contract->readv(buffers, actual, token);
        if (!result)
            co_return result.error;

        if (result.value > 0 && current)
            set_position(actual + static_cast<u64>(result.value));

        co_return result.value;
    }

    core::task<i64> open_file::writev(span<const buffer> buffers, u64 offset,
                                      core::cltoken token) {
        u64 sized = 0;

        if (!buffer_bytes(buffers, sized))
            co_return error_t::erange;

        if (sized == 0)
            co_return i64{0};

        if (!_parent)
            co_return error_t::ebadf;

        const bool current = offset == offset_current;
        const bool append =
            (_flags & static_cast<u32>(open_file_status_flags::append)) != 0;
        u64 actual = current ? current_position() : offset;

        if (append) {
            const long reserved = reserve_append(static_cast<u32>(sized), &actual);
            if (reserved < 0)
                co_return error_from_errno(-reserved);
        }

        auto contract = _parent->as_writable();
        if (!contract)
            co_return error_t::ebadf;

        const bool nonblock =
            (_flags & static_cast<u32>(open_file_status_flags::nonblock)) != 0;

        auto result = co_await contract->writev(buffers, actual, token, nonblock);
        if (!result)
            co_return result.error;

        if (result.value > 0 && (current || append))
            set_position(actual + static_cast<u64>(result.value));

        co_return result.value;
    }

    core::task<i64> open_file::read(span<u8> bytes, u64 offset, core::cltoken token) {
        if (bytes.size() > static_cast<usize>(~u32{0}))
            co_return error_t::erange;

        fs::buffer entry{bytes.data(), bytes.size()};
        auto result = co_await readv(span<const fs::buffer>(&entry, 1), offset, token);
        if (!result)
            co_return result.error;

        co_return result.value;
    }

    core::task<i64> open_file::write(span<const u8> bytes, u64 offset,
                                     core::cltoken token) {
        if (bytes.size() > static_cast<usize>(~u32{0}))
            co_return error_t::erange;

        fs::buffer entry{const_cast<u8*>(bytes.data()), bytes.size()};
        auto result = co_await writev(span<const fs::buffer>(&entry, 1), offset, token);
        if (!result)
            co_return result.error;

        co_return result.value;
    }

    core::task<i64> open_file::read_exact(span<u8> bytes, u64 offset,
                                          core::cltoken token) {
        auto result = co_await read(bytes, offset, token);
        if (!result)
            co_return result.error;

        if (result.value != static_cast<i64>(bytes.size()))
            co_return error_t::eio;

        co_return result.value;
    }
} // namespace astia::fs
