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

    namespace {
        /// Handlers are coroutines, but a submit batch holds the ring lock
        /// across every operation.  Run the body to its first suspension;
        /// until the lock-release protocol lands, a suspended body means the
        /// SQE was not consumed and the frame is discarded.
        bool run_operation(core::task<aio_result> operation, astia_rings* ring,
                           const astia_sqe& sqe) {
            if (!operation.valid())
                return false;
            operation.resume();
            if (!operation.done())
                return false;
            const aio_result result = *operation.value();
            switch (result.kind()) {
            case aio_result::state::retry:
                return false;
            case aio_result::state::pending:
                return true;
            case aio_result::state::complete:
                return ring::cq_post_guarded(ring, sqe.user_data, result.value());
            }
            return false;
        }
    } // namespace

    bool dispatch(ring::ring_slot& slot, astia_rings* ring, const astia_sqe& sqe) {
        if (sqe.op >= opcode_count)
            return ring::cq_post_guarded(ring, sqe.user_data, -ENOSYS);
        for (usize i = 0; i < size(); ++i) {
            const operation_descriptor& entry = entries()[i];
            if (static_cast<u32>(entry.operation) == sqe.op)
                return run_operation(entry.invoke(slot, ring, sqe), ring, sqe);
        }
        return ring::cq_post_guarded(ring, sqe.user_data, -ENOSYS);
    }

} // namespace astia::io::aio::registry

namespace astia::io::aio {

    bool validate_descriptors(const operation_descriptor* entries, usize count) {
        return validate_manifest(entries, count);
    }

} // namespace astia::io::aio
