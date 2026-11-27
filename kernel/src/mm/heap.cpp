#include <astia/arch/cpu.hpp>
#include <astia/arch/spinlock_guard.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/init.hpp>
#include <astia/memory.hpp>
#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/heap.hpp>

#include "heap_internal.hpp"

// A dropped guard must never return: the kernel overrides this weak default
// with a reporter that logs and panics.
namespace astia::mm::heap::detail {
    [[noreturn]] __attribute__((weak)) void
    report_fatal(const char* where, const char* reason, u64 block, u64 base, usz bytes) {
        (void) where;
        (void) reason;
        (void) block;
        (void) base;
        (void) bytes;

        __builtin_trap();
    }
} // namespace astia::mm::heap::detail

// A first-fit free-list allocator over one contiguous virtual window.
//
// init() takes frames from the kernel frame allocator and wires each one
// into the window with the arch-provided mapper, so mm never learns what a
// page map is; the window itself lives at a kernel address the arch picked.
// Everything past that is plain list surgery under one spinlock.
namespace astia::mm::heap {
    namespace {
        using detail::block;
        using detail::block_status;
        using detail::window;

        core::spinlock _lock{};

        u8*    _base         = nullptr;
        usz    _bytes        = 0;
        map_fn _mapper       = nullptr;
        block* _head         = nullptr;
        usz    _used         = 0;
        bool   _ready        = false;
        bool   _initializing = false;

        /// Wire the whole region into the page map, one frame at a time. A
        /// failure here means the machine is out of memory; the caller fails
        /// the boot (the already-mapped frames are not worth unwinding).
        bool map_backing(u8* base, usz bytes, map_fn mapper) {
            for (usz off = 0; off < bytes; off += page_bytes) {
                const u64 frame = kernel_frames().alloc(1, frame_flags::zeroed);

                if (frame == frame_allocator::invalid_frame)
                    return false;

                if (!mapper(reinterpret_cast<u64>(base) + off, frame))
                    return false;
            }

            return true;
        }

        /// Carve `need` bytes off `b` when the leftover could stand on its
        /// own (header + at least 16 payload bytes). `b` must already have
        /// been validated by the caller; subtraction keeps the arithmetic
        /// overflow-free.
        void split(block* b, usz need) {
            if (need > b->size)
                return;

            if ((b->size - need) < sizeof(block) + detail::alignment)
                return;

            auto* next =
                reinterpret_cast<block*>(reinterpret_cast<u8*>(b) + sizeof(block) + need);

            next->size = b->size - need - sizeof(block);
            next->free = true;
            next->prev = b;
            next->next = b->next;

            if (next->next != nullptr)
                next->next->prev = next;

            b->next = next;
            b->size = need;
        }

        /// Report a broken free-list link and stop before dereferencing it.
        /// The heap lock is dropped first so logging cannot deadlock against
        /// a logging CPU that is itself waiting for the heap.
        [[noreturn]] void corrupt(arch::interrupt_spinlock_guard& guard,
                                  const char* where, const block* b, const char* reason) {
            guard.unlock();
            detail::report_fatal(
                where, reason, static_cast<u64>(reinterpret_cast<usz>(b)),
                static_cast<u64>(reinterpret_cast<usz>(_base)), static_cast<u64>(_bytes));
        }

        /// Validate one block against the live window, then turn a failure
        /// into the heap's panic path.
        void validate_block(arch::interrupt_spinlock_guard& guard, const window& w,
                            const char* where, const block* b, bool adjacency) {
            const auto status = detail::validate(w, b, adjacency);

            if (status != block_status::ok)
                corrupt(guard, where, b, detail::status_name(status));
        }

        /// Merge `b` with its free neighbours (forward first, then let the
        /// predecessor absorb the result).
        void coalesce(arch::interrupt_spinlock_guard& guard, const window& w, block* b) {
            validate_block(guard, w, "coalesce", b, true);

            if (b->next != nullptr)
                validate_block(guard, w, "coalesce", b->next, true);

            if (b->prev != nullptr)
                validate_block(guard, w, "coalesce", b->prev, true);

            if (b->next != nullptr && b->next->free) {
                b->size += sizeof(block) + b->next->size;
                b->next = b->next->next;

                if (b->next != nullptr)
                    b->next->prev = b;

                validate_block(guard, w, "coalesce", b, true);
            }

            if (b->prev != nullptr && b->prev->free) {
                b->prev->size += sizeof(block) + b->size;
                b->prev->next = b->next;

                if (b->next != nullptr)
                    b->next->prev = b->prev;

                validate_block(guard, w, "coalesce", b->prev, true);
            }
        }
    } // namespace

    void configure(void* base, usz bytes, map_fn map) {
        arch::interrupt_spinlock_guard guard(_lock);

        if (_initializing)
            return;

        _base   = static_cast<u8*>(base);
        _bytes  = bytes;
        _mapper = map;
        _head   = nullptr;
        _used   = 0;
        _ready  = false;
    }

    bool init() {
        arch::interrupt_spinlock_guard guard(_lock);

        if (_base == nullptr || _mapper == nullptr || _bytes < sizeof(block) + 16)
            return false;

        if (_initializing)
            return false;

        // Reserve the configuration while mapping invokes the frame allocator
        // and arch callback. No partially built free list is published, and
        // configure() cannot replace this snapshot until mapping finishes.
        _initializing  = true;
        u8* const base = _base;

        const usz bytes = _bytes;

        const map_fn mapper = _mapper;

        guard.unlock();

        const bool mapped = map_backing(base, bytes, mapper);

        arch::interrupt_spinlock_guard publish(_lock);

        _initializing = false;

        if (!mapped)
            return false;

        _head       = reinterpret_cast<block*>(_base);
        _head->size = _bytes - sizeof(block);
        _head->prev = nullptr;
        _head->next = nullptr;
        _head->free = true;
        _used       = 0;
        _ready      = true;

        return true;
    }

    bool ready() {
        arch::interrupt_spinlock_guard guard(_lock);

        return _ready && !_initializing;
    }

    optional<u8*> alloc(usz bytes) {
        if (bytes == 0)
            return error_t::einval;

        if (bytes > detail::max_value - (detail::alignment - 1))
            return error_t::enomem;

        const usz need = align_up(bytes, detail::alignment);

        arch::interrupt_spinlock_guard guard(_lock);

        if (!_ready || _initializing) {
            // Not configured/initialised yet: nothing to hand out.
            return error_t::enomem;
        }

        usz walked = 0;

        const window w{_base, _bytes};

        if (!w.valid())
            corrupt(guard, "alloc", _head, detail::status_name(block_status::bad_window));

        const usz budget = w.max_blocks();

        for (block* b = _head; b != nullptr; b = b->next) {
            if (walked++ >= budget)
                corrupt(guard, "alloc", b, detail::status_name(block_status::list_cycle));

            validate_block(guard, w, "alloc", b, false);

            if (b->free && b->size >= need) {
                validate_block(guard, w, "alloc", b, true);

                split(b, need);

                validate_block(guard, w, "alloc", b, true);

                b->free = false;
                _used += b->size;

                return reinterpret_cast<u8*>(b) + sizeof(block);
            }
        }

        return error_t::enomem;
    }

    optional<u8*> alloc_zeroed(usz bytes) {
        auto result = alloc(bytes);
        if (result)
            fill_zero(result.value, bytes);

        return result;
    }

    void free(void* ptr) {
        if (ptr == nullptr)
            return;

        arch::interrupt_spinlock_guard guard(_lock);

        if (!_ready || _initializing) {
            return; // foreign pointer: ignore rather than corrupt
        }

        // Do not subtract from an arbitrary pointer before proving that it
        // belongs to this heap.  Apart from accepting interior pointers, that
        // would make a foreign pointer invoke undefined pointer arithmetic.
        const usz address = reinterpret_cast<usz>(ptr);

        usz walked = 0;

        const window w{_base, _bytes};

        if (!w.valid())
            corrupt(guard, "free", _head, detail::status_name(block_status::bad_window));

        const usz budget = w.max_blocks();

        for (block* b = _head; b != nullptr; b = b->next) {
            if (walked++ >= budget)
                corrupt(guard, "free", b, detail::status_name(block_status::list_cycle));

            validate_block(guard, w, "free", b, false);

            const usz payload = reinterpret_cast<usz>(b) + sizeof(block);
            if (payload != address)
                continue;

            if (b->free) {
                return; // double free: ignore
            }

            _used -= b->size;
            b->free = true;

            coalesce(guard, w, b);

            return;
        }
    }

    usz used() {
        arch::interrupt_spinlock_guard guard(_lock);

        return _used;
    }

    usz capacity() {
        arch::interrupt_spinlock_guard guard(_lock);

        return _bytes >= sizeof(block) + 16 ? _bytes - sizeof(block) : 0;
    }

    static bool step_heap(init::context&) {
        return init();
    }
} // namespace astia::mm::heap

ASTIA_INIT_STEP(memory, heap, astia::mm::heap::step_heap, "paging")
