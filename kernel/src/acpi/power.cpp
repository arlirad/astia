#include "power.hpp"

namespace astia::acpi::detail {
    namespace {
        constexpr u16 sleep_type_shift = 10;
        constexpr u16 sleep_enable     = 1u << 13;
        constexpr u16 sleep_type_mask  = 0x7;

        constexpr u32 fadt_dsdt_offset           = 40;
        constexpr u32 fadt_pm1a_control_offset   = 64;
        constexpr u32 fadt_pm1b_control_offset   = 68;
        constexpr u32 fadt_pm1_control_length    = 89;
        constexpr u32 fadt_x_pm1a_control_offset = 172;
        constexpr u32 fadt_x_pm1b_control_offset = 184;
        constexpr u32 fadt_x_control_end         = 196;

        generic_address load_gas(const u8* bytes) {
            return {
                bytes[0], bytes[1], bytes[2], bytes[3], load_u64(bytes + 4),
            };
        }

        bool gas_is_zero(const generic_address& gas) {
            return gas.space == 0 && gas.address == 0 && gas.bit_width == 0 &&
                   gas.bit_offset == 0 && gas.access_size == 0;
        }

        bool validate_gas(const generic_address& gas, power_error& error) {
            if (gas.space != gas_system_memory && gas.space != gas_system_io) {
                error = power_error::unsupported_address_space;

                return false;
            }

            if (gas.bit_width != 16 || gas.bit_offset != 0 ||
                (gas.access_size != 0 && gas.access_size != 2)) {
                error = power_error::unsupported_access_size;

                return false;
            }

            if (gas.address == 0) {
                error = power_error::invalid_pm1a;

                return false;
            }

            if (gas.space == gas_system_io && gas.address > 0xFFFF) {
                error = power_error::invalid_pm1a;

                return false;
            }

            return true;
        }

        bool package_element(interpreter& aml, const value& package, u32 index,
                             u64& out) {
            if (index >= package.count || package.first == no_value)
                return false;

            value_id current = package.first;

            for (u32 i = 0; i < index; ++i) {
                const value* entry = aml.get(current);

                if (entry == nullptr)
                    return false;

                current = entry->next;
            }

            return aml.integer(current, out);
        }
    } // namespace

    const char* power_error_text(power_error error) {
        switch (error) {
        case power_error::none:
            return "ok";
        case power_error::no_fadt:
            return "FADT absent";
        case power_error::short_fadt:
            return "FADT too short";
        case power_error::invalid_s5:
            return "_S5 absent or invalid";
        case power_error::invalid_package:
            return "_S5 package";
        case power_error::invalid_sleep_type:
            return "_S5 sleep type";
        case power_error::invalid_pm1a:
            return "PM1a control register";
        case power_error::invalid_pm1b:
            return "PM1b control register";
        case power_error::unsupported_address_space:
            return "PM1 address space";
        case power_error::unsupported_access_size:
            return "PM1 access size";
        case power_error::write_failed:
            return "PM1 write";
        }

        return "unknown";
    }

    bool build_s5_plan(const table_set& tables, interpreter& aml, s5_plan& out,
                       power_error& error) {
        out   = s5_plan{};
        error = power_error::none;

        if (tables.fadt.bytes.empty()) {
            error = power_error::no_fadt;

            return false;
        }

        const u8*   fadt   = tables.fadt.bytes.data();
        const usize length = tables.fadt.bytes.size();

        if (length < 90) {
            error = power_error::short_fadt;

            return false;
        }

        value_id s5 = no_value;

        if (!aml.evaluate_s5(s5)) {
            error = power_error::invalid_s5;

            return false;
        }

        const value* package = aml.get(s5);

        if (package == nullptr || package->kind != static_cast<u8>(value_kind::package) ||
            package->count < 2) {
            error = power_error::invalid_package;

            return false;
        }

        u64 slp_typa = 0;
        u64 slp_typb = 0;

        if (!package_element(aml, *package, 0, slp_typa) ||
            !package_element(aml, *package, 1, slp_typb)) {
            error = power_error::invalid_package;

            return false;
        }

        if (slp_typa > sleep_type_mask || slp_typb > sleep_type_mask) {
            error = power_error::invalid_sleep_type;

            return false;
        }

        out.slp_typa = static_cast<u16>(slp_typa);
        out.slp_typb = static_cast<u16>(slp_typb);

        const u8 pm1_control_length = fadt[fadt_pm1_control_length];

        if (pm1_control_length < 2) {
            error = power_error::invalid_pm1a;

            return false;
        }

        if (length >= 184) {
            const generic_address x_pm1a = load_gas(fadt + fadt_x_pm1a_control_offset);

            if (!gas_is_zero(x_pm1a)) {
                if (!validate_gas(x_pm1a, error)) {
                    error = power_error::invalid_pm1a;

                    return false;
                }

                out.pm1a.gas   = x_pm1a;
                out.pm1a.valid = true;
                out.extended   = true;
            }
        }

        if (!out.pm1a.valid) {
            const u32 address = load_u32(fadt + fadt_pm1a_control_offset);

            if (address == 0 || address > 0xFFFF || pm1_control_length < 2) {
                error = power_error::invalid_pm1a;

                return false;
            }

            out.pm1a.gas = {
                gas_system_io, 16, 0, 2, address,
            };
            out.pm1a.valid = true;
        }

        if (length >= fadt_x_control_end) {
            const generic_address x_pm1b = load_gas(fadt + fadt_x_pm1b_control_offset);

            if (!gas_is_zero(x_pm1b)) {
                if (!validate_gas(x_pm1b, error)) {
                    error = power_error::invalid_pm1b;

                    return false;
                }

                out.pm1b.gas   = x_pm1b;
                out.pm1b.valid = true;
            }
        }

        const u32 legacy_pm1b = length >= fadt_pm1b_control_offset + 4
                                    ? load_u32(fadt + fadt_pm1b_control_offset)
                                    : 0;

        if (!out.pm1b.valid && legacy_pm1b != 0) {
            if (legacy_pm1b > 0xFFFF || pm1_control_length < 2) {
                error = power_error::invalid_pm1b;

                return false;
            }

            out.pm1b.gas = {
                gas_system_io, 16, 0, 2, legacy_pm1b,
            };
            out.pm1b.valid = true;
        }

        if (out.slp_typb != 0 && !out.pm1b.valid) {
            error = power_error::invalid_pm1b;

            return false;
        }

        return true;
    }

    bool execute_s5(const s5_plan& plan, const pm_io& io, power_error& error) {
        error = power_error::none;

        if (!plan.pm1a.valid) {
            error = power_error::invalid_pm1a;

            return false;
        }

        const u16 value_a =
            static_cast<u16>((plan.slp_typa << sleep_type_shift) | sleep_enable);

        if (plan.pm1a.gas.space == gas_system_io) {
            if (io.write_io == nullptr ||
                !io.write_io(io.context, static_cast<u16>(plan.pm1a.gas.address),
                             value_a)) {
                error = power_error::write_failed;

                return false;
            }
        }
        else if (plan.pm1a.gas.space == gas_system_memory) {
            if (io.write_memory == nullptr ||
                !io.write_memory(io.context, plan.pm1a.gas.address, value_a)) {
                error = power_error::write_failed;

                return false;
            }
        }
        else {
            error = power_error::unsupported_address_space;

            return false;
        }

        if (plan.pm1b.valid) {
            const u16 value_b =
                static_cast<u16>((plan.slp_typb << sleep_type_shift) | sleep_enable);

            if (plan.pm1b.gas.space == gas_system_io) {
                if (io.write_io == nullptr ||
                    !io.write_io(io.context, static_cast<u16>(plan.pm1b.gas.address),
                                 value_b)) {
                    error = power_error::write_failed;

                    return false;
                }
            }
            else if (plan.pm1b.gas.space == gas_system_memory) {
                if (io.write_memory == nullptr ||
                    !io.write_memory(io.context, plan.pm1b.gas.address, value_b)) {
                    error = power_error::write_failed;

                    return false;
                }
            }
            else {
                error = power_error::unsupported_address_space;

                return false;
            }
        }

        return true;
    }
} // namespace astia::acpi::detail
