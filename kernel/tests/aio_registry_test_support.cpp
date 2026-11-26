#include "aio_registry_test_support.hpp"

#include <astia/api/errno.h>
#include <astia/arch/interrupts.hpp>
#include <astia/atomic.hpp>
#include <astia/core/resume_queue.hpp>
#include <astia/error.hpp>
#include <astia/mm/heap.hpp>
#include <astia/mm/iovec.hpp>
#include <astia/optional.hpp>
#include <astia/span.hpp>

#include "../src/io/aio/aio.hpp"
#include "../src/io/aio/dispatch.hpp"
#include "../src/io/aio/memory.hpp"
#include "../src/io/aio/ring.hpp"
#include "../src/ipc/pipe.hpp"
#include "../src/ipc/unix_socket.hpp"
#include "../src/proc/proc.hpp"
#include "../src/proc/process.hpp"

#include <cstdlib>

namespace astia::io::aio::test_support {
    namespace detail {
        using fs::file_mode;
        using fs::file_type;
        using fs::open_file;

        struct descriptor_entry {
            mm::shptr<open_file> file;
            u32                  flags = 0;
        };

        alignas(proc::process) unsigned char process_storage[sizeof(proc::process)]{};
        proc::process*       current = nullptr;
        descriptor_entry     descriptors[proc::max_process_fds];
        mm::shptr<open_file> open_file_value;
        mm::shptr<open_file> pipe_read_value;
        mm::shptr<open_file> pipe_write_value;
        mm::shptr<open_file> socket_value;
        mm::shptr<open_file> socketpair_first;
        mm::shptr<open_file> socketpair_second;
        file_mode            node_mode{file_type::regular, fs::default_file_permissions};
        bool                 memory_allowed = true;
        chown_record         last_chown_path_record;
        constexpr usize      node_override_capacity = 8;
        constexpr usize      node_override_path_max = 128;

        struct node_override_entry {
            char      path[node_override_path_max]{};
            file_mode mode{};
            uid_t     uid{};
            gid_t     gid{};
            bool      mode_set  = false;
            bool      owner_set = false;
        };

        node_override_entry node_overrides[node_override_capacity];

        bool same_path(const char* left, const char* right) {
            while (*left != '\0' && *left == *right) {
                ++left;
                ++right;
            }

            return *left == *right;
        }

        void copy_path(char* destination, const char* source) {
            usize length = 0;

            while (length + 1 < node_override_path_max && source[length] != '\0') {
                destination[length] = source[length];

                ++length;
            }

            destination[length] = '\0';
        }

        node_override_entry* find_override(const char* path, bool create) {
            if (path == nullptr)
                return nullptr;

            node_override_entry* empty = nullptr;

            for (auto& entry : node_overrides) {
                if (entry.path[0] != '\0' && same_path(entry.path, path))
                    return &entry;

                if (empty == nullptr && entry.path[0] == '\0')
                    empty = &entry;
            }

            return create ? empty : nullptr;
        }

        node_override_entry* ensure_override(const char* path) {
            node_override_entry* entry = find_override(path, true);

            if (entry != nullptr && entry->path[0] == '\0')
                copy_path(entry->path, path);

            return entry;
        }

        u64 timer_hz_value    = 1000;
        u64 timer_ticks_value = 0;
        u64 generation_value  = 0;

        bool inside_range(span<u8> range) {
            return memory_allowed && (!range.null() || range.empty());
        }

        bool is_valid_fd(i32 fd) {
            return fd >= 0 && static_cast<u32>(fd) < proc::max_process_fds;
        }

        bool user_span_ok(proc::process*, span<u8> range) {
            return inside_range(range);
        }

        bool user_span_writable_ok(proc::process*, span<u8> range) {
            return inside_range(range);
        }

        bool kernel_span_ok(span<u8>) {
            return true;
        }

        long pin_user_span(proc::process*, span<u8> range, ::astia::io::direction,
                           mm::iovec& out, bool) {
            if (!inside_range(range))
                return -EFAULT;

            if (range.empty())
                return 0;

            return out.append_kernel(range.data(), range.length()) ? 0 : -ENOMEM;
        }

        bool copy_in(proc::process*, void* destination, span<u8> source) {
            if (!inside_range(source) || destination == nullptr || source.empty())
                return false;

            __builtin_memcpy(destination, source.data(), source.length());

            return true;
        }

        bool copy_in_string(proc::process*, span<char> destination, span<u8> source) {
            if (!inside_range(source) || destination.null() || destination.empty() ||
                source.null())
                return false;

            const usize limit =
                source.empty() ? destination.length() - 1 : source.length();
            usize length = 0;

            while (length < limit && length + 1 < destination.length()) {
                const char value = static_cast<char>(source[length]);

                destination[length] = value;

                ++length;

                if (value == '\0')
                    return true;
            }

            return false;
        }

        bool copy_out(proc::process*, span<u8> destination, const void* source) {
            if (!inside_range(destination) || source == nullptr)
                return false;

            if (destination.empty())
                return true;

            __builtin_memcpy(destination.data(), source, destination.length());

            return true;
        }

        bool realtime_now(u64& sec, u64& nsec) {
            sec  = 100;
            nsec = 0;

            return true;
        }
    } // namespace detail

    using namespace detail;

    cq_capture::cq_capture() {
        ring.cq_off =
            static_cast<u32>(reinterpret_cast<u8*>(&cq) - reinterpret_cast<u8*>(&ring));
        ring.cqes_off =
            static_cast<u32>(reinterpret_cast<u8*>(cqes) - reinterpret_cast<u8*>(&ring));
        cq.entries = 64;
        cq.mask    = 63;
    }

    u32 cq_capture::count() const {
        const u32 head =
            astia::atomic_ref<u32>(cq.head).load(astia::memory_order::acquire);
        const u32 tail =
            astia::atomic_ref<u32>(cq.tail).load(astia::memory_order::acquire);

        return tail - head;
    }

    const astia_cqe* cq_capture::at(u32 index) const {
        const u32 head =
            astia::atomic_ref<u32>(cq.head).load(astia::memory_order::acquire);

        return &cqes[(head + index) & cq.mask];
    }

    i32 cq_capture::result(u32 index) const {
        return at(index)->result;
    }

    u64 cq_capture::user_data(u32 index) const {
        return at(index)->user_data;
    }

    void cq_capture::consume(u32 count) {
        const u32 head =
            astia::atomic_ref<u32>(cq.head).load(astia::memory_order::acquire);

        astia::atomic_ref<u32>(cq.head).store(head + count, astia::memory_order::release);
    }

    void reset_environment() {
        if (current != nullptr) {
            (void) current->seteuid(0);
            (void) current->setegid(0);
        }

        for (auto& entry : descriptors) {
            entry = {};
        }

        for (auto& entry : node_overrides) {
            entry = {};
        }

        open_file_value   = {};
        pipe_read_value   = {};
        pipe_write_value  = {};
        socket_value      = {};
        socketpair_first  = {};
        socketpair_second = {};
        node_mode         = file_mode(file_type::regular, fs::default_file_permissions);
        memory_allowed    = true;
        last_chown_path_record = {};
        timer_hz_value         = 1000;
        timer_ticks_value      = 0;
        generation_value       = 0;
        current                = nullptr;
    }

    const chown_record& last_chown_path() {
        return detail::last_chown_path_record;
    }

    void install_current_process() {
        if (current == nullptr)
            current = reinterpret_cast<proc::process*>(process_storage);

        if (!descriptors[0].file) {
            auto standard = mm::share<open_file>(mm::shptr<fs::file>{}, 0u);

            for (i32 fd = 0; fd < 3; ++fd) {
                descriptors[fd].file = standard;
            }
        }
    }

    void clear_current_process() {
        current = nullptr;
    }

    i32 install_descriptor(const mm::shptr<open_file>& file, u32 flags) {
        for (i32 fd = 3; fd < static_cast<i32>(proc::max_process_fds); ++fd) {
            if (!descriptors[fd].file) {
                descriptors[fd].file  = file;
                descriptors[fd].flags = flags;

                return fd;
            }
        }

        return -EMFILE;
    }

    bool descriptor_present(i32 fd) {
        return is_valid_fd(fd) && static_cast<bool>(descriptors[fd].file);
    }

    void clear_descriptor(i32 fd) {
        if (is_valid_fd(fd))
            descriptors[fd] = {};
    }

    void set_open_file(const mm::shptr<open_file>& file) {
        open_file_value = file;
    }

    void set_pipe_files(const mm::shptr<open_file>& read_end,
                        const mm::shptr<open_file>& write_end) {
        pipe_read_value  = read_end;
        pipe_write_value = write_end;
    }

    void set_socket_file(const mm::shptr<open_file>& file) {
        socket_value = file;
    }

    void set_socketpair_files(const mm::shptr<open_file>& first,
                              const mm::shptr<open_file>& second) {
        socketpair_first  = first;
        socketpair_second = second;
    }

    void set_node_mode(file_mode mode) {
        node_mode = mode;
    }

    void set_node_mode(const char* path, file_mode mode) {
        node_override_entry* entry = ensure_override(path);
        if (entry == nullptr)
            return;

        entry->mode     = mode;
        entry->mode_set = true;
    }

    void set_node_owner(const char* path, uid_t uid, gid_t gid) {
        node_override_entry* entry = ensure_override(path);
        if (entry == nullptr)
            return;

        entry->uid       = uid;
        entry->gid       = gid;
        entry->owner_set = true;
    }

    void set_process_credentials(uid_t uid, gid_t gid) {
        if (current == nullptr)
            return;

        (void) current->seteuid(raw_id(uid));
        (void) current->setegid(raw_id(gid));
    }

    void set_memory_allowed(bool allowed) {
        memory_allowed = allowed;
    }

    void set_timer(u64 hz, u64 ticks) {
        timer_hz_value    = hz;
        timer_ticks_value = ticks;
    }

    void add_timer_ticks(u64 ticks) {
        timer_ticks_value += ticks;
    }

    void release_parked(ring::ring_slot& slot) {
        for (u32 i = 0; i < ring::parked_operation_slots; ++i) {
            ring::parked_op& op = slot.park[i];

            if (!op.used)
                continue;

            if (!op.done && op.operation != nullptr) {
                op.operation->request().cancel_device(false,
                                                      core::cancel_reason::shutdown);
                op.operation->request().acknowledge();
            }

            ring::clear_armed(slot, i);
            ring::clear_signalable(slot, i);
            op.clear();
        }
    }
} // namespace astia::io::aio::test_support

namespace astia::io::aio {
    const user_memory_adapter& user_memory() {
        static const user_memory_adapter value = {
            ::astia::io::aio::test_support::detail::user_span_ok,
            ::astia::io::aio::test_support::detail::user_span_writable_ok,
            ::astia::io::aio::test_support::detail::kernel_span_ok,
            ::astia::io::aio::test_support::detail::pin_user_span,
            ::astia::io::aio::test_support::detail::copy_in,
            ::astia::io::aio::test_support::detail::copy_in_string,
            ::astia::io::aio::test_support::detail::copy_out,
            ::astia::io::aio::test_support::detail::realtime_now,
        };

        return value;
    }
} // namespace astia::io::aio

namespace astia::io::aio::ring {
    void ring_lock::acquire() {
        const u32 cpu = arch::cpu::current_index();
        if (cpu < cpu_slots)
            ++depth[cpu];
    }

    void ring_lock::release() {
        const u32 cpu = arch::cpu::current_index();

        if (cpu < cpu_slots && depth[cpu] != 0)
            --depth[cpu];
    }

    ring_guard::ring_guard(ring_lock& value) : _lock(value) {
        _lock.acquire();
    }

    ring_guard::~ring_guard() {
        if (_held)
            _lock.release();
    }

    void ring_guard::unlock() {
        if (_held) {
            _lock.release();

            _held = false;
        }
    }

    void ring_guard::release_lock() {
        if (_held) {
            _lock.release();

            _held = false;
        }
    }

    bool cq_post(astia_rings* ring, u64 user_data, i32 result) {
        astia_cq_ring* cq   = astia_rings_cq(ring);
        astia_cqe*     cqes = astia_rings_cqes(ring);

        const u32 head =
            astia::atomic_ref<u32>(cq->head).load(astia::memory_order::acquire);
        const u32 tail =
            astia::atomic_ref<u32>(cq->tail).load(astia::memory_order::relaxed);

        if (tail - head >= cq->entries)
            return false;

        cqes[tail & cq->mask] = {user_data, result, 0};

        astia::atomic_ref<u32>(cq->tail).store(tail + 1, astia::memory_order::release);

        return true;
    }

    bool cq_post_guarded(astia_rings* ring, u64 user_data, i32 result) {
        return cq_post(ring, user_data, result);
    }

    bool cq_has_capacity(const ring_slot&, const astia_rings* ring, u32 count) {
        if (ring == nullptr)
            return false;

        astia_cq_ring* cq = astia_rings_cq(const_cast<astia_rings*>(ring));

        if (count > cq->entries)
            return false;

        const u32 head = astia::atomic_ref(cq->head).load(astia::memory_order::acquire);
        const u32 tail = astia::atomic_ref(cq->tail).load(astia::memory_order::relaxed);

        return tail - head <= cq->entries - count;
    }

    u64 ready_generation() {
        return ++::astia::io::aio::test_support::detail::generation_value;
    }

    void publish_waiter_hint(const ring_slot&, u32, const fs::file*) {}
} // namespace astia::io::aio::ring

namespace astia::io::aio {
    void io_device_ready_context(const io_device*) {}
} // namespace astia::io::aio

namespace astia::sys {
    void io_device_ready_context(const ::astia::io::aio::io_device*) {}
} // namespace astia::sys

namespace astia::arch {
    u64 timer_ticks() {
        return ::astia::io::aio::test_support::detail::timer_ticks_value;
    }

    u32 timer_hz() {
        return static_cast<u32>(::astia::io::aio::test_support::detail::timer_hz_value);
    }
} // namespace astia::arch

namespace astia::core {
    bool schedule_resume(std::coroutine_handle<>, u64) {
        return false;
    }

    void cancel_resume(std::coroutine_handle<>, u64) {}
} // namespace astia::core

namespace astia::mm::heap {
    optional<u8*> alloc(usize bytes) {
        if (bytes == 0)
            return error_t::einval;

        void* value = std::malloc(bytes);
        if (value == nullptr)
            return error_t::enomem;

        return static_cast<u8*>(value);
    }

    void free(void* value) {
        std::free(value);
    }
} // namespace astia::mm::heap

namespace astia::mm {
    iovec::iovec(iovec&& other) noexcept {
        for (usize i = 0; i < other._count; ++i) {
            _segments[i].base   = other._segments[i].base;
            _segments[i].offset = other._segments[i].offset;
            _segments[i].length = other._segments[i].length;
        }

        _count           = other._count;
        _total           = other._total;
        _index           = other._index;
        _offset          = other._offset;
        _remaining       = other._remaining;
        other._count     = 0;
        other._total     = 0;
        other._index     = 0;
        other._offset    = 0;
        other._remaining = 0;
    }

    iovec& iovec::operator=(iovec&& other) noexcept {
        if (this != &other) {
            release();

            for (usize i = 0; i < other._count; ++i) {
                _segments[i].base   = other._segments[i].base;
                _segments[i].offset = other._segments[i].offset;
                _segments[i].length = other._segments[i].length;
            }

            _count           = other._count;
            _total           = other._total;
            _index           = other._index;
            _offset          = other._offset;
            _remaining       = other._remaining;
            other._count     = 0;
            other._total     = 0;
            other._index     = 0;
            other._offset    = 0;
            other._remaining = 0;
        }

        return *this;
    }

    void iovec::release() {
        _count     = 0;
        _total     = 0;
        _index     = 0;
        _offset    = 0;
        _remaining = 0;
    }

    usize iovec::bytes() const {
        return _total;
    }

    bool iovec::done() const {
        return _remaining == 0;
    }

    void* iovec::window() const {
        if (_remaining == 0 || _index >= _count)
            return nullptr;

        const pinned_segment& segment = _segments[_index];

        return segment.base + segment.offset + _offset;
    }

    usize iovec::chunk() const {
        if (_remaining == 0 || _index >= _count)
            return 0;

        const pinned_segment& segment = _segments[_index];

        const usize left = segment.length - _offset;

        return left < _remaining ? left : _remaining;
    }

    usize iovec::advance(usize bytes) {
        usize moved = 0;

        while (moved < bytes && _remaining != 0 && _index < _count) {
            const usize take = chunk();
            if (take == 0)
                break;

            const usize step = take < bytes - moved ? take : bytes - moved;

            _offset += step;
            _remaining -= step;
            moved += step;

            if (_offset == _segments[_index].length) {
                ++_index;

                _offset = 0;
            }
        }

        return moved;
    }

    bool iovec::append_kernel(void* address, usize length) {
        if (_count >= max_segments || length == 0)
            return _count < max_segments;

        pinned_segment& segment = _segments[_count];

        segment.base   = static_cast<u8*>(address);
        segment.offset = 0;
        segment.length = length;

        ++_count;

        _total += length;
        _remaining += length;

        return true;
    }
} // namespace astia::mm

namespace astia::proc {
    process* current_process() {
        return ::astia::io::aio::test_support::detail::current;
    }

    i64 process::seteuid(u32 value) {
        arch::interrupt_spinlock_guard guard(_metadata_lock);

        if (_euid != 0 && value != _uid && value != _euid)
            return -EPERM;

        _euid = value;

        return 0;
    }

    i64 process::setegid(u32 value) {
        arch::interrupt_spinlock_guard guard(_metadata_lock);

        if (_egid != 0 && value != _gid && value != _egid)
            return -EPERM;

        _egid = value;

        return 0;
    }

    i64 acquire_controlling_terminal(const mm::shptr<fs::open_file>&) {
        return 0;
    }

    bool has_controlling_terminal() {
        return false;
    }

    mm::shptr<fs::open_file> process::descriptor(fd_t fd) const {
        if (!::astia::io::aio::test_support::is_valid_fd(fd.value))
            return {};

        return ::astia::io::aio::test_support::detail::descriptors[fd.value].file;
    }

    bool process::set_descriptor(fd_t fd, const mm::shptr<fs::open_file>& value) {
        return set_descriptor(fd, value, 0);
    }

    bool process::set_descriptor(fd_t fd, const mm::shptr<fs::open_file>& value,
                                 u32 flags) {
        if (!::astia::io::aio::test_support::is_valid_fd(fd.value))
            return false;

        ::astia::io::aio::test_support::detail::descriptors[fd.value].file  = value;
        ::astia::io::aio::test_support::detail::descriptors[fd.value].flags = flags;

        return true;
    }

    bool process::clear_descriptor(fd_t fd) {
        if (!::astia::io::aio::test_support::is_valid_fd(fd.value))
            return false;

        ::astia::io::aio::test_support::detail::descriptors[fd.value] = {};

        return true;
    }

    optional<fd_t> process::allocate_descriptor(const mm::shptr<fs::open_file>& value,
                                                u32                             flags) {
        if (!value)
            return error_t::emfile;

        for (i32 fd = 0; fd < static_cast<i32>(max_process_fds); ++fd) {
            if (!::astia::io::aio::test_support::detail::descriptors[fd].file) {
                ::astia::io::aio::test_support::detail::descriptors[fd].file  = value;
                ::astia::io::aio::test_support::detail::descriptors[fd].flags = flags;

                return fd_t{fd};
            }
        }

        return error_t::emfile;
    }

    optional<fd_t> process::duplicate_descriptor(fd_t old_fd) {
        return duplicate_descriptor(old_fd, fd_t{0}, 0);
    }

    optional<fd_t> process::duplicate_descriptor(fd_t old_fd, fd_t minimum, u32 flags) {
        if (!::astia::io::aio::test_support::is_valid_fd(old_fd.value) ||
            !::astia::io::aio::test_support::detail::descriptors[old_fd.value].file)
            return error_t::ebadf;

        for (i32 fd = minimum.value; fd < static_cast<i32>(max_process_fds); ++fd) {
            if (!::astia::io::aio::test_support::detail::descriptors[fd].file) {
                ::astia::io::aio::test_support::detail::descriptors[fd].file =
                    ::astia::io::aio::test_support::detail::descriptors[old_fd.value]
                        .file;
                ::astia::io::aio::test_support::detail::descriptors[fd].flags = flags;

                return fd_t{fd};
            }
        }

        return error_t::emfile;
    }

    u32 process::descriptor_flags(fd_t fd) const {
        if (!::astia::io::aio::test_support::is_valid_fd(fd.value))
            return 0;

        return ::astia::io::aio::test_support::detail::descriptors[fd.value].flags;
    }

    bool process::set_descriptor_flags(fd_t fd, u32 flags) {
        if (!::astia::io::aio::test_support::is_valid_fd(fd.value) ||
            !::astia::io::aio::test_support::detail::descriptors[fd.value].file)
            return false;

        ::astia::io::aio::test_support::detail::descriptors[fd.value].flags = flags;

        return true;
    }

    optional<fd_t> process::duplicate_descriptor_to(fd_t old_fd, fd_t new_fd) {
        if (!::astia::io::aio::test_support::is_valid_fd(old_fd.value) ||
            !::astia::io::aio::test_support::is_valid_fd(new_fd.value) ||
            !::astia::io::aio::test_support::detail::descriptors[old_fd.value].file)
            return error_t::ebadf;

        ::astia::io::aio::test_support::detail::descriptors[new_fd.value].file =
            ::astia::io::aio::test_support::detail::descriptors[old_fd.value].file;
        ::astia::io::aio::test_support::detail::descriptors[new_fd.value].flags =
            ::astia::io::aio::test_support::detail::descriptors[old_fd.value].flags;

        return new_fd;
    }

    i64 process::compose_path(const char* path, span<char> buffer) const {
        if (path == nullptr || buffer.null() || buffer.empty())
            return -EINVAL;

        usize length = 0;

        while (path[length] != '\0') {
            ++length;
        }

        if (length + 1 > buffer.length())
            return -ENAMETOOLONG;

        for (usize i = 0; i <= length; ++i) {
            buffer[i] = path[i];
        }

        return static_cast<i64>(length);
    }

    i64 process::compose_path_at(fd_t, const char* path, span<char> buffer) const {
        return compose_path(path, buffer);
    }

    i64 process::resolve_path(const char* path, span<char> buffer, bool) const {
        return compose_path(path, buffer);
    }

    i64 process::resolve_path_at(fd_t, const char* path, span<char> buffer, bool) const {
        return compose_path(path, buffer);
    }

    bool process::in_group(gid_t group) const {
        if (group.value == _egid)
            return true;

        for (u32 i = 0; i < _group_count; ++i) {
            if (_groups[i] == group)
                return true;
        }

        return false;
    }
} // namespace astia::proc

namespace astia::fs {
    open_file::~open_file() {}

    long open_file::reserve_append(u32, u64* offset) {
        if (offset == nullptr)
            return -EINVAL;

        *offset = 0;

        return 0;
    }

    file_mode open_file::mode() const {
        return _parent ? _parent->mode() : _mode;
    }

    u32 open_file::poll(u32 events) const {
        if (!_parent)
            return 0;

        auto contract = _parent->as_pollable();
        if (contract)
            return contract->poll(events, _flags);

        u32 ready = 0;

        if (_parent->as_readable())
            ready |= POLLIN;

        if (_parent->as_writable())
            ready |= POLLOUT;

        return ready & events;
    }

    long open_file::submit(io::request& request) {
        return _parent ? _parent->submit(request) : -EBADF;
    }

    long open_file::size() const {
        return _parent ? _parent->size() : -EBADF;
    }

    long open_file::truncate(u64 length) {
        if (!_parent)
            return -EBADF;

        auto contract = _parent->as_truncatable();
        if (!contract)
            return -EINVAL;

        auto result = detail::await_now(contract->truncate(length, {}));

        return result ? static_cast<long>(result.value) : error_errno(result.error);
    }

    long open_file::ioctl(u64 request, void* argument, usize size) {
        if (!_parent)
            return -EBADF;

        auto contract = _parent->as_ioctlable();
        if (!contract)
            return -ENOTTY;

        auto result = detail::await_now(contract->ioctl(request, argument, size, {}));

        return result ? static_cast<long>(result.value) : error_errno(result.error);
    }

    long open_file::acquire_controlling_terminal(i32 group) {
        return _parent ? _parent->acquire_controlling_terminal(group) : -EBADF;
    }

    u64 open_file::mtime_sec() const {
        return _parent ? _parent->mtime_sec() : 0;
    }

    u64 open_file::mtime_nsec() const {
        return _parent ? _parent->mtime_nsec() : 0;
    }

    long open_file::chmod(file_permissions permissions) {
        return _parent ? _parent->chmod(permissions) : -EBADF;
    }

    mm::shptr<open_file> open(const char*, u32) {
        return ::astia::io::aio::test_support::open_file_value;
    }

    mm::shptr<open_file> open_directory(const char*) {
        return ::astia::io::aio::test_support::open_file_value;
    }

    mm::shptr<open_file> open_path(const char*, u32) {
        return ::astia::io::aio::test_support::open_file_value;
    }

    bool read_only_path(const char*) {
        return false;
    }

    long resolve_path(const char* path, char* buffer, u32 capacity, bool) {
        if (path == nullptr || buffer == nullptr)
            return -EINVAL;

        usize length = 0;

        while (path[length] != '\0') {
            ++length;
        }

        if (length + 1 > capacity)
            return -ENAMETOOLONG;

        for (usize i = 0; i <= length; ++i) {
            buffer[i] = path[i];
        }

        return 0;
    }

    file_type node_type(const char* path) {
        return path == nullptr ? file_type::unknown
                               : ::astia::io::aio::test_support::detail::node_mode.type();
    }

    file_mode node_mode(const char* path) {
        if (path == nullptr)
            return {};

        auto* entry = ::astia::io::aio::test_support::detail::find_override(path, false);

        return entry != nullptr && entry->mode_set
                   ? entry->mode
                   : ::astia::io::aio::test_support::detail::node_mode;
    }

    uid_t node_uid(const char* path) {
        auto* entry = ::astia::io::aio::test_support::detail::find_override(path, false);

        return entry != nullptr && entry->owner_set ? entry->uid : uid_t{};
    }

    gid_t node_gid(const char* path) {
        auto* entry = ::astia::io::aio::test_support::detail::find_override(path, false);

        return entry != nullptr && entry->owner_set ? entry->gid : gid_t{};
    }

    long chmod_path(const char*, file_permissions) {
        return 0;
    }

    long chown_path(const char*, uid_t uid, gid_t gid, bool follow_final) {
        ::astia::io::aio::test_support::detail::last_chown_path_record = {true, uid, gid,
                                                                          follow_final};

        return 0;
    }

    long touch_path(const char*, u64, u64) {
        return 0;
    }

    long make_directory(const char*, file_mode) {
        return 0;
    }

    long remove_path(const char*, bool) {
        return 0;
    }

    long rename_path(const char*, const char*) {
        return 0;
    }

    long link_path(const char*, const char*) {
        return 0;
    }

    long make_symlink(const char*, const char*) {
        return 0;
    }

    long read_symlink(const char*, char*, u32) {
        return 0;
    }

    long read_directory(const char*, u64, char* name, u32 capacity, file_type* type) {
        if (name == nullptr || capacity == 0)
            return -EINVAL;

        name[0] = 'x';
        name[1] = '\0';

        if (type != nullptr)
            *type = ::astia::io::aio::test_support::detail::node_mode.type();

        return 1;
    }

    long read_directory(const mm::shptr<open_file>& directory, u64 index, char* name,
                        u32 capacity, file_type* type) {
        if (!directory)
            return -EBADF;

        return read_directory(nullptr, index, name, capacity, type);
    }

    optional<pipe_endpoints> create_pipe() {
        if (!::astia::io::aio::test_support::detail::pipe_read_value ||
            !::astia::io::aio::test_support::detail::pipe_write_value)
            return error_t::emfile;

        return pipe_endpoints{::astia::io::aio::test_support::detail::pipe_read_value,
                              ::astia::io::aio::test_support::detail::pipe_write_value};
    }

    long create_unix_socket(mm::shptr<open_file>& out, u32) {
        if (!::astia::io::aio::test_support::detail::socket_value)
            return -EAFNOSUPPORT;

        out = ::astia::io::aio::test_support::detail::socket_value;

        return 0;
    }

    long unix_socketpair(mm::shptr<open_file>& first, mm::shptr<open_file>& second, u32) {
        if (!::astia::io::aio::test_support::detail::socketpair_first ||
            !::astia::io::aio::test_support::detail::socketpair_second)
            return -EMFILE;

        first  = ::astia::io::aio::test_support::detail::socketpair_first;
        second = ::astia::io::aio::test_support::detail::socketpair_second;

        return 0;
    }
} // namespace astia::fs

namespace astia::io::aio::ring {
    bool park_detached_cqe(ring_slot& slot, u64 user_data, i32 result) {
        for (u32 i = 0; i < kernel_ring_max_entries; ++i) {
            if (slot.pending_detached[i].used)
                continue;

            slot.pending_detached[i] = {user_data, result, true};

            return true;
        }

        return false;
    }
} // namespace astia::io::aio::ring
