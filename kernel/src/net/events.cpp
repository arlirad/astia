#include <astia/net/events.hpp>

namespace astia::net::events {
    namespace {
        struct link_bound_tag;
        using registry = core::hook::event<link_bound_tag, link&>;
    } // namespace

    bool link_bound::add(const char* id, core::hook::priority_t priority, handler call) {
        return registry::add(id, priority, call);
    }

    bool link_bound::remove(const char* id) {
        return registry::remove(id);
    }

    bool link_bound::remove_quiesced(const char* id) {
        return registry::remove_quiesced(id);
    }

    void link_bound::emit(link& value) {
        registry::emit(value);
    }
} // namespace astia::net::events
