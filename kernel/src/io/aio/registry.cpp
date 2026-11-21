#include "registry.hpp"

#include <astia/api/errno.h>

#include "ring.hpp"

namespace astia::io::aio::registry {

    extern "C" {
    extern const operation_descriptor __start_astia_aio_handlers[];
    extern const operation_descriptor __stop_astia_aio_handlers[];
    }

    const operation_descriptor* entries() {
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
            const operation_descriptor& entry = entries()[i];
            if (static_cast<u32>(entry.operation) == sqe.op)
                return entry.invoke(slot, ring, sqe);
        }
        return ring::cq_post_guarded(ring, sqe.user_data, -ENOSYS);
    }

} // namespace astia::io::aio::registry

namespace astia::io::aio {

    bool validate_descriptors(const operation_descriptor* entries, usize count) {
        return validate_manifest(entries, count);
    }

} // namespace astia::io::aio
