#include "registry.hpp"

#include <astia/api/errno.h>

#include "ring.hpp"

namespace astia::io::aio::registry {

    extern "C" {
    extern const descriptor __start_astia_aio_handlers[];
    extern const descriptor __stop_astia_aio_handlers[];
    }

    const descriptor* entries() {
        return __start_astia_aio_handlers;
    }

    usize size() {
        return static_cast<usize>(__stop_astia_aio_handlers - __start_astia_aio_handlers);
    }

    bool valid() {
        return validate_descriptors(entries(), size());
    }

    bool dispatch(ring::ring_slot& slot, astia_rings* ring, const astia_sqe& sqe) {
        if (sqe.op >= opcode_count)
            return ring::cq_post_guarded(ring, sqe.user_data, -ENOSYS);
        for (usize i = 0; i < size(); ++i) {
            const descriptor& entry = entries()[i];
            if (static_cast<u32>(entry.operation) == sqe.op)
                return entry.invoke(slot, ring, sqe);
        }
        return ring::cq_post_guarded(ring, sqe.user_data, -ENOSYS);
    }

} // namespace astia::io::aio::registry

namespace astia::io::aio {

    bool validate_descriptors(const descriptor* entries, usize count) {
        if (entries == nullptr || count != opcode_count)
            return false;
        bool seen[opcode_count]{};
        for (usize i = 0; i < count; ++i) {
            const u32 value = static_cast<u32>(entries[i].operation);
            if (!valid_opcode(entries[i].operation) || entries[i].invoke == nullptr ||
                seen[value])
                return false;
            seen[value] = true;
        }
        for (bool present : seen)
            if (!present)
                return false;
        return true;
    }

} // namespace astia::io::aio
