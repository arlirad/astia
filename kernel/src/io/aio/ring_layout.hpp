#pragma once

#include <astia/api/aio.h>
#include <astia/atomic.hpp>
#include <astia/common.hpp>

namespace astia::io::aio::ring {
    /// Immutable ring geometry cached in kernel-private storage at setup.
    /// Every kernel access derives its pointers and bounds from this copy, so
    /// rewrites of the user-mapped header cannot redirect kernel addressing.
    struct ring_layout {
        u32 sq_entries = 0;
        u32 cq_entries = 0;
        u32 sq_off     = 0;
        u32 cq_off     = 0;
        u32 sqes_off   = 0;
        u32 cqes_off   = 0;
        u32 sq_mask    = 0;
        u32 cq_mask    = 0;

        static constexpr u32 max_entries    = ASTIA_AIO_MAX_RING_ENTRIES;
        static constexpr u32 min_cq_entries = 64;

        static ring_layout make(u32 sq, u32 cq) {
            ring_layout layout{};

            layout.sq_entries = sq;
            layout.cq_entries = cq;
            layout.sq_mask    = sq - 1;
            layout.cq_mask    = cq - 1;
            layout.sq_off     = static_cast<u32>(sizeof(astia_rings));
            layout.cq_off     = layout.sq_off + static_cast<u32>(sizeof(astia_sq_ring));
            layout.sqes_off   = layout.cq_off + static_cast<u32>(sizeof(astia_cq_ring));
            layout.cqes_off = layout.sqes_off + static_cast<u32>(sizeof(astia_sqe)) * sq;

            return layout;
        }

        bool valid() const {
            const u32 expected_cq =
                sq_entries * 2 < min_cq_entries ? min_cq_entries : sq_entries * 2;

            return sq_entries != 0 && sq_entries <= max_entries &&
                   (sq_entries & (sq_entries - 1)) == 0 && cq_entries == expected_cq &&
                   sq_mask == sq_entries - 1 && cq_mask == cq_entries - 1 &&
                   sq_off == sizeof(astia_rings) &&
                   cq_off == sq_off + sizeof(astia_sq_ring) &&
                   sqes_off == cq_off + sizeof(astia_cq_ring) &&
                   cqes_off == sqes_off + sizeof(astia_sqe) * sq_entries;
        }

        astia_rings* header(u8* base) const {
            return reinterpret_cast<astia_rings*>(base);
        }

        const astia_rings* header(const u8* base) const {
            return reinterpret_cast<const astia_rings*>(base);
        }

        astia_sq_ring* sq(u8* base) const {
            return reinterpret_cast<astia_sq_ring*>(base + sq_off);
        }

        const astia_sq_ring* sq(const u8* base) const {
            return reinterpret_cast<const astia_sq_ring*>(base + sq_off);
        }

        astia_cq_ring* cq(u8* base) const {
            return reinterpret_cast<astia_cq_ring*>(base + cq_off);
        }

        const astia_cq_ring* cq(const u8* base) const {
            return reinterpret_cast<const astia_cq_ring*>(base + cq_off);
        }

        astia_sqe* sqe(u8* base, u32 index) const {
            return reinterpret_cast<astia_sqe*>(base + sqes_off +
                                                static_cast<u32>(sizeof(astia_sqe)) *
                                                    (index & sq_mask));
        }

        const astia_sqe* sqe(const u8* base, u32 index) const {
            return reinterpret_cast<const astia_sqe*>(
                base + sqes_off +
                static_cast<u32>(sizeof(astia_sqe)) * (index & sq_mask));
        }

        astia_cqe* cqe(u8* base, u32 index) const {
            return reinterpret_cast<astia_cqe*>(base + cqes_off +
                                                static_cast<u32>(sizeof(astia_cqe)) *
                                                    (index & cq_mask));
        }

        const astia_cqe* cqe(const u8* base, u32 index) const {
            return reinterpret_cast<const astia_cqe*>(
                base + cqes_off +
                static_cast<u32>(sizeof(astia_cqe)) * (index & cq_mask));
        }

        /// True while the shared header and the ring-owned sizes still match
        /// the geometry cached at setup.
        bool header_matches(const u8* base) const {
            if (!valid())
                return false;

            astia_rings* r =
                const_cast<astia_rings*>(reinterpret_cast<const astia_rings*>(base));

            if (atomic_ref(r->magic).load(memory_order::relaxed) != RINGS_MAGIC ||
                atomic_ref(r->version).load(memory_order::relaxed) != API_VERSION ||
                atomic_ref(r->features).load(memory_order::relaxed) != FEAT_SINGLE_MMAP ||
                atomic_ref(r->sq_entries).load(memory_order::relaxed) != sq_entries ||
                atomic_ref(r->cq_entries).load(memory_order::relaxed) != cq_entries ||
                atomic_ref(r->sq_off).load(memory_order::relaxed) != sq_off ||
                atomic_ref(r->cq_off).load(memory_order::relaxed) != cq_off ||
                atomic_ref(r->sqes_off).load(memory_order::relaxed) != sqes_off ||
                atomic_ref(r->cqes_off).load(memory_order::relaxed) != cqes_off)
                return false;

            astia_sq_ring* s = const_cast<astia_sq_ring*>(
                reinterpret_cast<const astia_sq_ring*>(base + sq_off));
            astia_cq_ring* c = const_cast<astia_cq_ring*>(
                reinterpret_cast<const astia_cq_ring*>(base + cq_off));

            return atomic_ref(s->entries).load(memory_order::relaxed) == sq_entries &&
                   atomic_ref(s->mask).load(memory_order::relaxed) == sq_mask &&
                   atomic_ref(c->entries).load(memory_order::relaxed) == cq_entries &&
                   atomic_ref(c->mask).load(memory_order::relaxed) == cq_mask;
        }

        u32 sq_pending(const u8* base) const {
            const astia_sq_ring* s = sq(base);

            const u32 tail = atomic_ref(const_cast<astia_sq_ring*>(s)->tail)
                                 .load(memory_order::acquire);
            const u32 head = atomic_ref(const_cast<astia_sq_ring*>(s)->head)
                                 .load(memory_order::relaxed);

            return tail - head;
        }

        u32 cq_pending(const u8* base) const {
            const astia_cq_ring* c = cq(base);

            const u32 tail = atomic_ref(const_cast<astia_cq_ring*>(c)->tail)
                                 .load(memory_order::acquire);
            const u32 head = atomic_ref(const_cast<astia_cq_ring*>(c)->head)
                                 .load(memory_order::relaxed);

            return tail - head;
        }

        bool sq_cursors_ok(const u8* base) const {
            return valid() && sq_pending(base) <= sq_entries;
        }

        bool cq_cursors_ok(const u8* base) const {
            return valid() && cq_pending(base) <= cq_entries;
        }

        /// Publish one completion using only cached geometry.  The shared
        /// header is treated as untrusted: a rewritten layout fails the post
        /// instead of steering the store.
        bool post_cqe(u8* base, u64 user_data, i32 result) const {
            if (!header_matches(base) || !cq_cursors_ok(base))
                return false;

            astia_cq_ring* c = cq(base);

            const u32 head = atomic_ref(c->head).load(memory_order::acquire);
            const u32 tail = atomic_ref(c->tail).load(memory_order::relaxed);

            if (tail - head >= cq_entries)
                return false;

            *cqe(base, tail) = {user_data, result, 0};

            atomic_ref(c->tail).store(tail + 1, memory_order::release);

            return true;
        }
    };
} // namespace astia::io::aio::ring
