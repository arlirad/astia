#include "registry.hpp"

#include <astia/api/errno.h>

#include "ring.hpp"
#include "runner.hpp"

namespace astia::io::aio::registry {
    extern "C" {
    extern const operation_descriptor __start_astia_aio_handlers[];
    extern const operation_descriptor __stop_astia_aio_handlers[];
    }

    const operation_descriptor* entries() {
        return __start_astia_aio_handlers;
    }

    usz size() {
        return static_cast<usz>(__stop_astia_aio_handlers - __start_astia_aio_handlers);
    }

    namespace {
        /// One slot per opcode, filled from the link section on first use.
        /// Dispatch is then a single indexed load instead of a scan of every
        /// descriptor, matching the syscall table's flat layout.
        const operation_descriptor* table[opcode_count] = {};
        bool                        registry_ready      = false;

        bool build_table() {
            if (size() != opcode_count)
                return false;

            for (usz i = 0; i < size(); ++i) {
                const operation_descriptor& entry = entries()[i];

                if (!valid_opcode(entry.operation) || entry.invoke == nullptr)
                    return false;

                const u32 index = static_cast<u32>(entry.operation);

                if (table[index] != nullptr)
                    return false;

                table[index] = &entry;
            }

            for (const operation_descriptor* entry : table) {
                if (entry == nullptr)
                    return false;
            }

            registry_ready = true;

            return true;
        }
    } // namespace

    bool valid() {
        return registry_ready || build_table();
    }

    bool dispatch(ring::ring_slot& slot, astia_rings* ring, const astia_sqe& sqe) {
        if (!registry_ready && !build_table())
            return ring::cq_post_guarded(ring, sqe.user_data, -ENOSYS);

        if (sqe.op >= opcode_count)
            return ring::cq_post_guarded(ring, sqe.user_data, -ENOSYS);

        const operation_descriptor* entry = table[sqe.op];
        if (entry == nullptr)
            return ring::cq_post_guarded(ring, sqe.user_data, -ENOSYS);

        auto state = runner::make_state(slot, sqe.user_data);
        if (!state)
            return false;

        const core::cltoken token = runner::token(state.get());

        if (token.canceled())
            return ring::cq_post_guarded(ring, sqe.user_data, -ECANCELED);

        return runner::launch(entry->invoke(slot, ring, sqe, token),
                              static_cast<mm::shptr<runner::detached_state>&&>(state),
                              slot, ring, sqe);
    }
} // namespace astia::io::aio::registry

namespace astia::io::aio {
    bool validate_descriptors(const operation_descriptor* entries, usz count) {
        return validate_manifest(entries, count);
    }
} // namespace astia::io::aio
