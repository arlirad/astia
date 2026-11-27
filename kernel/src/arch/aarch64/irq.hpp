#pragma once

#include <astia/common.hpp>

// Generic boot-CPU local IRQ routing contract for AArch64, following GICv3
// IHI 0069G:
// - INTIDs 0-15 are SGIs, 16-31 are PPIs, 32-1019 are SPIs, and 1020-1023 are
//   special (Table 2-1, sections 2.2 and 2.2.1). Only SGIs and PPIs have a
//   boot-CPU route; an unregistered local INTID, every SPI, and special
//   INTIDs 1020-1022 are fatal. 1023 is the spurious no-op.
// - One handler owns each local INTID. Registration rejects INTIDs outside
//   0-31, null handlers, and duplicate ownership. Unregistration refuses
//   while a dispatch is in flight, so a handler cannot disappear between
//   acknowledge and completion.
// - The router invokes the owner and reports the result; it never writes EOI
//   or DIR. The GIC layer completes a handled interrupt only after the
//   handler returns, preserving handler-before-priority-drop ordering
//   (section 4.1.1).
// - The scheduler handoff is a single post-IRQ callback slot. The scheduler
//   owns the policy; the router only invokes the callback from the IRQ-exit
//   boundary, never from inside a handler. It refuses to run while a
//   dispatch is in flight.
// - Registration and the handoff slot are boot-CPU only and run with
//   PSTATE.I set. The table, counters, and slot are fixed, allocation-free,
//   and lock-free; there is no SPI, SMP, or nested-dispatch support.
namespace astia::arch::aarch64::irq {
    inline constexpr u32 intid_mask          = 0x00ffffff;
    inline constexpr u32 max_sgi_id          = 15;
    inline constexpr u32 first_ppi_id        = 16;
    inline constexpr u32 max_local_intid     = 31;
    inline constexpr u32 first_spi_id        = 32;
    inline constexpr u32 last_spi_id         = 1019;
    inline constexpr u32 special_intid_first = 1020;
    inline constexpr u32 special_intid_last  = 1023;
    inline constexpr u32 spurious_intid      = 1023;
    inline constexpr u32 local_intid_limit   = 32;

    enum class intid_class : u8 {
        sgi,
        ppi,
        spi,
        special,
        invalid,
    };

    [[nodiscard]] constexpr intid_class classify_intid(u32 intid) {
        if (intid <= max_sgi_id)
            return intid_class::sgi;

        if (intid < first_spi_id)
            return intid_class::ppi;

        if (intid <= last_spi_id)
            return intid_class::spi;

        if (intid <= special_intid_last)
            return intid_class::special;

        return intid_class::invalid;
    }

    [[nodiscard]] constexpr bool valid_local_intid(u32 intid) {
        return intid < local_intid_limit;
    }

    enum class route_error : u8 {
        none,
        invalid_intid,
        null_handler,
        already_registered,
        not_registered,
        busy,
    };

    [[nodiscard]] constexpr const char* describe(route_error value) {
        switch (value) {
        case route_error::none:
            return "none";
        case route_error::invalid_intid:
            return "invalid-intid";
        case route_error::null_handler:
            return "null-handler";
        case route_error::already_registered:
            return "already-registered";
        case route_error::not_registered:
            return "not-registered";
        case route_error::busy:
            return "busy";
        }

        return "unknown";
    }

    using handler = void (*)(void* context);

    struct route {
        handler callback = nullptr;
        void*   context  = nullptr;
        u32     intid    = 0;
    };

    enum class dispatch_action : u8 {
        spurious,
        handled,
        fatal,
    };

    struct dispatch_result {
        dispatch_action action = dispatch_action::fatal;
        u32             intid  = 0;
        u32             iar    = 0;
        route           owner{};
    };

    using handoff = void (*)();

    enum class handoff_error : u8 {
        none,
        null_callback,
        already_registered,
        not_registered,
        busy,
    };

    [[nodiscard]] constexpr const char* describe(handoff_error value) {
        switch (value) {
        case handoff_error::none:
            return "none";
        case handoff_error::null_callback:
            return "null-callback";
        case handoff_error::already_registered:
            return "already-registered";
        case handoff_error::not_registered:
            return "not-registered";
        case handoff_error::busy:
            return "busy";
        }

        return "unknown";
    }

    struct evidence {
        u32 dispatches = 0;
        u32 handled    = 0;
        u32 spurious   = 0;
        u32 fatal      = 0;
        u32 handoffs   = 0;
    };

    inline route    routes[local_intid_limit]{};
    inline u32      dispatch_depth   = 0;
    inline handoff  handoff_callback = nullptr;
    inline evidence counters{};

    [[nodiscard]] inline route_error register_route(u32 intid, handler callback,
                                                    void* context) {
        if (!valid_local_intid(intid))
            return route_error::invalid_intid;

        if (callback == nullptr)
            return route_error::null_handler;

        route& slot = routes[intid];

        if (slot.callback != nullptr)
            return route_error::already_registered;

        slot = route{callback, context, intid};

        return route_error::none;
    }

    [[nodiscard]] inline route_error unregister_route(u32 intid) {
        if (!valid_local_intid(intid))
            return route_error::invalid_intid;

        if (routes[intid].callback == nullptr)
            return route_error::not_registered;

        if (dispatch_depth != 0)
            return route_error::busy;

        routes[intid] = route{};

        return route_error::none;
    }

    [[nodiscard]] inline bool registered(u32 intid) {
        return valid_local_intid(intid) && routes[intid].callback != nullptr;
    }

    [[nodiscard]] inline dispatch_result dispatch(u32 iar) {
        const u32 intid = iar & intid_mask;

        dispatch_result result{dispatch_action::fatal, intid, iar, route{}};

        ++counters.dispatches;

        if (intid == spurious_intid) {
            ++counters.spurious;

            result.action = dispatch_action::spurious;

            return result;
        }

        if (!registered(intid)) {
            ++counters.fatal;

            return result;
        }

        result.owner = routes[intid];

        ++dispatch_depth;
        result.owner.callback(result.owner.context);
        --dispatch_depth;

        ++counters.handled;

        result.action = dispatch_action::handled;

        return result;
    }

    [[nodiscard]] inline handoff_error set_handoff(handoff callback) {
        if (callback == nullptr)
            return handoff_error::null_callback;

        if (dispatch_depth != 0)
            return handoff_error::busy;

        if (handoff_callback != nullptr)
            return handoff_error::already_registered;

        handoff_callback = callback;

        return handoff_error::none;
    }

    [[nodiscard]] inline handoff_error clear_handoff() {
        if (handoff_callback == nullptr)
            return handoff_error::not_registered;

        if (dispatch_depth != 0)
            return handoff_error::busy;

        handoff_callback = nullptr;

        return handoff_error::none;
    }

    [[nodiscard]] inline bool run_handoff() {
        if (dispatch_depth != 0 || handoff_callback == nullptr)
            return false;

        ++counters.handoffs;
        handoff_callback();

        return true;
    }

    [[nodiscard]] inline const evidence& stats() {
        return counters;
    }

    inline void reset() {
        for (u32 index = 0; index < local_intid_limit; ++index) {
            routes[index] = route{};
        }

        dispatch_depth   = 0;
        handoff_callback = nullptr;
        counters         = evidence{};
    }
} // namespace astia::arch::aarch64::irq
