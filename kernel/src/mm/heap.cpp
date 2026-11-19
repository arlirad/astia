#include <astia/core/spinlock.hpp>
#include <astia/init.hpp>
#include <astia/memory.hpp>
#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/heap.hpp>

// A first-fit free-list allocator over one contiguous virtual window.
//
// init() takes frames from the kernel frame allocator and wires each one
// into the window with the arch-provided mapper, so mm never learns what a
// page map is; the window itself lives at a kernel address the arch picked.
// Everything past that is plain list surgery under one spinlock.
namespace astia::mm::heap {

    namespace {

        /// One block: header directly precedes the payload. sizeof() is a
        /// multiple of 16, so a 16-aligned region start keeps every payload
        /// 16-byte aligned.
        struct block {
            usize  size; // payload bytes
            block* prev;
            block* next;
            bool   free;
        };

        static_assert(sizeof(block) % 16 == 0,
                      "block header must keep 16-byte alignment");

        core::spinlock _lock{};

        u8*    _base         = nullptr;
        usize  _bytes        = 0;
        map_fn _mapper       = nullptr;
        block* _head         = nullptr;
        usize  _used         = 0;
        bool   _ready        = false;
        bool   _initializing = false;

        /// Wire the whole region into the page map, one frame at a time. A
        /// failure here means the machine is out of memory; the caller fails
        /// the boot (the already-mapped frames are not worth unwinding).
        bool map_backing(u8* base, usize bytes, map_fn mapper) {
            for (usize off = 0; off < bytes; off += page_bytes) {
                const u64 frame = kernel_frames().alloc(1, frame_flags::zeroed);
                if (frame == frame_allocator::invalid_frame) {
                    return false;
                }
                if (!mapper(reinterpret_cast<u64>(base) + off, frame)) {
                    return false;
                }
            }
            return true;
        }

        /// Carve `need` bytes off `b` when the leftover could stand on its
        /// own (header + at least 16 payload bytes).
        void split(block* b, usize need) {
            if (b->size < need + sizeof(block) + 16) {
                return;
            }
            auto* next =
                reinterpret_cast<block*>(reinterpret_cast<u8*>(b) + sizeof(block) + need);
            next->size = b->size - need - sizeof(block);
            next->free = true;
            next->prev = b;
            next->next = b->next;
            if (next->next != nullptr) {
                next->next->prev = next;
            }
            b->next = next;
            b->size = need;
        }

        /// Merge `b` with its free neighbours (forward first, then let the
        /// predecessor absorb the result).
        void coalesce(block* b) {
            if (b->next != nullptr && b->next->free) {
                b->size += sizeof(block) + b->next->size;
                b->next = b->next->next;
                if (b->next != nullptr) {
                    b->next->prev = b;
                }
            }
            if (b->prev != nullptr && b->prev->free) {
                b->prev->size += sizeof(block) + b->size;
                b->prev->next = b->next;
                if (b->next != nullptr) {
                    b->next->prev = b->prev;
                }
            }
        }

    } // namespace

    void configure(void* base, usize bytes, map_fn map) {
        core::spinlock_guard guard(_lock);
        if (_initializing) {
            return;
        }
        _base   = static_cast<u8*>(base);
        _bytes  = bytes;
        _mapper = map;
        _head   = nullptr;
        _used   = 0;
        _ready  = false;
    }

    bool init() {
        core::spinlock_guard guard(_lock);
        if (_base == nullptr || _mapper == nullptr || _bytes < sizeof(block) + 16) {
            return false;
        }
        if (_initializing) {
            return false;
        }

        // Reserve the configuration while mapping invokes the frame allocator
        // and arch callback. No partially built free list is published, and
        // configure() cannot replace this snapshot until mapping finishes.
        _initializing       = true;
        u8* const    base   = _base;
        const usize  bytes  = _bytes;
        const map_fn mapper = _mapper;
        guard.unlock();

        const bool mapped = map_backing(base, bytes, mapper);

        core::spinlock_guard publish(_lock);
        _initializing = false;
        if (!mapped) {
            return false;
        }

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
        core::spinlock_guard guard(_lock);
        return _ready && !_initializing;
    }

    optional<u8*> alloc(usize bytes) {
        if (bytes == 0) {
            return error_t::einval;
        }
        const usize need = align_up(bytes, 16);

        core::spinlock_guard guard(_lock);
        if (!_ready || _initializing) {
            // Not configured/initialised yet: nothing to hand out.
            return error_t::enomem;
        }
        for (block* b = _head; b != nullptr; b = b->next) {
            if (b->free && b->size >= need) {
                split(b, need);
                b->free = false;
                _used += b->size;
                return reinterpret_cast<u8*>(b) + sizeof(block);
            }
        }
        return error_t::enomem;
    }

    optional<u8*> alloc_zeroed(usize bytes) {
        auto result = alloc(bytes);
        if (result) {
            fill_zero(result.value, bytes);
        }
        return result;
    }

    void free(void* ptr) {
        if (ptr == nullptr) {
            return;
        }

        core::spinlock_guard guard(_lock);
        if (!_ready || _initializing) {
            return; // foreign pointer: ignore rather than corrupt
        }

        // Do not subtract from an arbitrary pointer before proving that it
        // belongs to this heap.  Apart from accepting interior pointers, that
        // would make a foreign pointer invoke undefined pointer arithmetic.
        const usize address = reinterpret_cast<usize>(ptr);
        for (block* b = _head; b != nullptr; b = b->next) {
            const usize payload = reinterpret_cast<usize>(b) + sizeof(block);
            if (payload != address) {
                continue;
            }
            if (b->free) {
                return; // double free: ignore
            }
            _used -= b->size;
            b->free = true;
            coalesce(b);
            return;
        }
    }

    usize used() {
        core::spinlock_guard guard(_lock);
        return _used;
    }

    usize capacity() {
        core::spinlock_guard guard(_lock);
        return _bytes >= sizeof(block) + 16 ? _bytes - sizeof(block) : 0;
    }

    static bool step_heap(init::context&) {
        return init();
    }

} // namespace astia::mm::heap

ASTIA_INIT_STEP(memory, heap, astia::mm::heap::step_heap, "paging")
