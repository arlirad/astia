#pragma once

#include "aml.hpp"

namespace astia::acpi::detail {
    inline constexpr u8 prt_no_irq = 0xff;

    enum class prt_error : u8 {
        none,
        no_root_bridge,
        no_prt,
        malformed_prt,
        no_route,
        unsupported_source,
        no_crs,
        malformed_crs,
        descriptor_out_of_range,
        link_unselected,
        srs_failed,
        irq_out_of_range,
    };

    const char* prt_error_text(prt_error error);

    struct prt_route {
        u8   irq    = prt_no_irq;
        u8   gsi    = 0;
        u8   pin    = 0;
        bool direct = false;
        bool valid  = false;
    };

    bool resolve_prt_route(interpreter& aml, u8 bus, u8 device, u8 function, u8 pin,
                           prt_route& out, prt_error& error);
} // namespace astia::acpi::detail
