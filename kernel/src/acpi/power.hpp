#pragma once

#include "aml.hpp"
#include "tables.hpp"

namespace astia::acpi::detail {
    constexpr u8 gas_system_memory = 0;
    constexpr u8 gas_system_io     = 1;

    struct generic_address {
        u8  space       = 0;
        u8  bit_width   = 0;
        u8  bit_offset  = 0;
        u8  access_size = 0;
        u64 address     = 0;
    };

    struct pm_register {
        generic_address gas{};
        bool            valid = false;
    };

    struct s5_plan {
        u16         slp_typa = 0;
        u16         slp_typb = 0;
        pm_register pm1a{};
        pm_register pm1b{};
        bool        extended = false;
    };

    enum class power_error : u8 {
        none,
        no_fadt,
        short_fadt,
        invalid_s5,
        invalid_package,
        invalid_sleep_type,
        invalid_pm1a,
        invalid_pm1b,
        unsupported_address_space,
        unsupported_access_size,
        write_failed,
    };

    const char* power_error_text(power_error error);

    struct pm_io {
        void* context                                                = nullptr;
        bool (*write_io)(void* context, u16 port, u16 value)         = nullptr;
        bool (*write_memory)(void* context, u64 physical, u16 value) = nullptr;
    };

    bool build_s5_plan(const table_set& tables, interpreter& aml, s5_plan& out,
                       power_error& error);

    bool execute_s5(const s5_plan& plan, const pm_io& io, power_error& error);
} // namespace astia::acpi::detail
