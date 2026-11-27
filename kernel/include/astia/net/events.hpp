#pragma once

#include <astia/core/hook.hpp>

namespace astia::net {
    class link;

    namespace events {
        /// Fired after a link is bound to a device.  Handlers run with no bus
        /// lock held, in the binding task's context.
        ///
        /// The registry lives in the kernel (events.cpp), not in this header:
        /// an inline-static template registry would give every loadable
        /// module its own copy, so module handlers would never see kernel
        /// emissions.
        class link_bound final {
            public:
            using handler = core::hook::outcome (*)(link&);

            static bool add(const char* id, core::hook::priority_t priority,
                            handler call);
            static bool remove(const char* id);
            static bool remove_quiesced(const char* id);
            static void emit(link& value);
        };
    } // namespace events
} // namespace astia::net
