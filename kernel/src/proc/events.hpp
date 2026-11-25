#pragma once

#include <astia/core/hook.hpp>

namespace astia::proc {

    class process;

    namespace events {

        /// Process teardown, emitted from ~process once the address space is
        /// gone.  Subsystems that own per-process resources register here so
        /// their accounting does not have to be hard-coded into the
        /// destructor.
        struct teardown : core::hook::event<teardown, process&> {};

        /// A fork has produced a child whose address space already mirrors
        /// the parent.  Handlers that keep per-process state return `stop`
        /// to veto the fork (the caller unwinds the registration).
        struct forked : core::hook::event<forked, process&, process&> {};

    } // namespace events

} // namespace astia::proc
