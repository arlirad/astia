#include "aio_registry_test_support.hpp"

#include "../src/io/aio/dispatch.hpp"
#include "../src/io/aio/filesystem.hpp"
#include "../src/io/aio/readiness.hpp"
#include "../src/io/aio/registry.hpp"
#include "../src/io/aio/socket.hpp"

namespace astia::io::aio::test_support {

    unsigned core_dispatches       = 0;
    unsigned filesystem_dispatches = 0;
    unsigned readiness_dispatches  = 0;
    unsigned socket_dispatches     = 0;
    unsigned unknown_completions   = 0;

    void reset_registry_counters() {
        core_dispatches       = 0;
        filesystem_dispatches = 0;
        readiness_dispatches  = 0;
        socket_dispatches     = 0;
        unknown_completions   = 0;
    }

    bool routes_to(owner expected, unsigned operation) {
        ring::ring_slot slot{};
        astia_rings     ring{};
        astia_sqe       sqe{};
        sqe.op = operation;
        reset_registry_counters();
        if (!registry::dispatch(slot, &ring, sqe))
            return false;
        const unsigned counts[]       = {core_dispatches, filesystem_dispatches,
                                         readiness_dispatches, socket_dispatches};
        const unsigned expected_index = static_cast<unsigned>(expected);
        for (unsigned i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i)
            if (counts[i] != (i == expected_index ? 1u : 0u))
                return false;
        return unknown_completions == (expected == owner::unknown ? 1u : 0u);
    }

} // namespace astia::io::aio::test_support

namespace astia::io::aio::dispatch {

    bool dispatch_core(ring::ring_slot&, astia_rings*, const astia_sqe&) {
        ++test_support::core_dispatches;
        return true;
    }

    mm::shptr<fs::open_file> file_for(i32) {
        return {};
    }

} // namespace astia::io::aio::dispatch

namespace astia::io::aio::filesystem {

    bool dispatch(ring::ring_slot&, astia_rings*, const astia_sqe&) {
        ++test_support::filesystem_dispatches;
        return true;
    }

} // namespace astia::io::aio::filesystem

namespace astia::io::aio::readiness {

    bool dispatch(ring::ring_slot&, astia_rings*, const astia_sqe&,
                  const mm::shptr<fs::open_file>&) {
        ++test_support::readiness_dispatches;
        return true;
    }

    bool dispatch_registered(ring::ring_slot& slot, astia_rings* ring,
                             const astia_sqe& sqe) {
        return dispatch(slot, ring, sqe, {});
    }

} // namespace astia::io::aio::readiness

namespace astia::io::aio::socket {

    bool dispatch(ring::ring_slot&, astia_rings*, const astia_sqe&) {
        ++test_support::socket_dispatches;
        return true;
    }

} // namespace astia::io::aio::socket

namespace astia::io::aio::ring {

    bool cq_post_guarded(astia_rings*, u64, i32) {
        ++test_support::unknown_completions;
        return true;
    }

} // namespace astia::io::aio::ring
