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

        core::spinlock lock_{};

        u8*    base_         = nullptr;
        usize  bytes_        = 0;
        map_fn mapper_       = nullptr;
        block* head_         = nullptr;
        usize  used_         = 0;
        bool   ready_        = false;
        bool   initializing_ = false;

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
        core::spinlock_guard guard(lock_);
        if (initializing_) {
            return;
        }
        base_   = static_cast<u8*>(base);
        bytes_  = bytes;
        mapper_ = map;
        head_   = nullptr;
        used_   = 0;
        ready_  = false;
    }

    bool init() {
        core::spinlock_guard guard(lock_);
        if (base_ == nullptr || mapper_ == nullptr || bytes_ < sizeof(block) + 16) {
            return false;
        }
        if (initializing_) {
            return false;
        }

        // Reserve the configuration while mapping invokes the frame allocator
        // and arch callback. No partially built free list is published, and
        // configure() cannot replace this snapshot until mapping finishes.
        initializing_       = true;
        u8* const    base   = base_;
        const usize  bytes  = bytes_;
        const map_fn mapper = mapper_;
        guard.unlock();

        const bool mapped = map_backing(base, bytes, mapper);

        core::spinlock_guard publish(lock_);
        initializing_ = false;
        if (!mapped) {
            return false;
        }

        head_       = reinterpret_cast<block*>(base_);
        head_->size = bytes_ - sizeof(block);
        head_->prev = nullptr;
        head_->next = nullptr;
        head_->free = true;
        used_       = 0;
        ready_      = true;
        return true;
    }

    bool ready() {
        core::spinlock_guard guard(lock_);
        return ready_ && !initializing_;
    }

    optional<u8*> alloc(usize bytes) {
        if (bytes == 0) {
            return error_t::einval;
        }
        const usize need = align_up(bytes, 16);

        core::spinlock_guard guard(lock_);
        if (!ready_ || initializing_) {
            // Not configured/initialised yet: nothing to hand out.
            return error_t::enomem;
        }
        for (block* b = head_; b != nullptr; b = b->next) {
            if (b->free && b->size >= need) {
                split(b, need);
                b->free = false;
                used_ += b->size;
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

        core::spinlock_guard guard(lock_);
        if (!ready_ || initializing_) {
            return; // foreign pointer: ignore rather than corrupt
        }

        // Do not subtract from an arbitrary pointer before proving that it
        // belongs to this heap.  Apart from accepting interior pointers, that
        // would make a foreign pointer invoke undefined pointer arithmetic.
        const usize address = reinterpret_cast<usize>(ptr);
        for (block* b = head_; b != nullptr; b = b->next) {
            const usize payload = reinterpret_cast<usize>(b) + sizeof(block);
            if (payload != address) {
                continue;
            }
            if (b->free) {
                return; // double free: ignore
            }
            used_ -= b->size;
            b->free = true;
            coalesce(b);
            return;
        }
    }

    usize used() {
        core::spinlock_guard guard(lock_);
        return used_;
    }

    usize capacity() {
        core::spinlock_guard guard(lock_);
        return bytes_ >= sizeof(block) + 16 ? bytes_ - sizeof(block) : 0;
    }

    static bool step_heap(init::context&) {
        return init();
    }

} // namespace astia::mm::heap

ASTIA_INIT_STEP(memory, heap, astia::mm::heap::step_heap, "paging")
