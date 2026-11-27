#include "power.hpp"

namespace astia::acpi::detail {
    namespace {
        constexpr u16 sleep_type_shift = 10;
        constexpr u16 sleep_enable     = 1u << 13;
        constexpr u16 sleep_type_mask  = 0x7;
        constexpr u16 sleep_type_field_mask =
            static_cast<u16>(sleep_type_mask << sleep_type_shift);
        constexpr u16 pm1_control_mask =
            static_cast<u16>(sleep_type_field_mask | sleep_enable);
        constexpr u32 fadt_pm1a_control_offset      = 64;
        constexpr u32 fadt_pm1b_control_offset      = 68;
        constexpr u32 fadt_pm1_control_length       = 89;
        constexpr u32 fadt_flags_offset             = 112;
        constexpr u32 fadt_reset_register_offset    = 116;
        constexpr u32 fadt_reset_value_offset       = 128;
        constexpr u32 fadt_reset_end                = 129;
        constexpr u32 fadt_x_pm1a_control_offset    = 172;
        constexpr u32 fadt_x_pm1b_control_offset    = 184;
        constexpr u32 fadt_x_control_end            = 196;
        constexpr u32 fadt_reset_register_supported = 1u << 10;

        bool reset_gas_valid(const generic_address& gas, power_error& error) {
            if (gas.space != gas_system_memory && gas.space != gas_system_io &&
                gas.space != gas_pci_configuration) {
                error = power_error::unsupported_address_space;

                return false;
            }

            if (gas.bit_width != 8 || gas.bit_offset != 0 ||
                (gas.access_size != 0 && gas.access_size != 1)) {
                error = power_error::unsupported_access_size;

                return false;
            }

            if (gas.address == 0 ||
                (gas.space == gas_system_io && gas.address > 0xFFFF)) {
                error = power_error::invalid_reset;

                return false;
            }

            if (gas.space == gas_pci_configuration) {
                const u16 reserved = static_cast<u16>(gas.address >> 48);
                const u16 device   = static_cast<u16>(gas.address >> 32);
                const u16 function = static_cast<u16>(gas.address >> 16);
                const u16 offset   = static_cast<u16>(gas.address);

                if (reserved != 0 || device >= 32 || function >= 8 || offset > 0xFFF) {
                    error = power_error::invalid_reset;

                    return false;
                }
            }

            return true;
        }

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

        bool read_pm1_control(const pm_register& reg, const pm_io& io, u16& value,
                              power_error& error) {
            if (reg.gas.space == gas_system_io) {
                if (io.read_io == nullptr ||
                    !io.read_io(io.context, static_cast<u16>(reg.gas.address), value)) {
                    error = power_error::read_failed;

                    return false;
                }
            }
            else if (reg.gas.space == gas_system_memory) {
                if (io.read_memory == nullptr ||
                    !io.read_memory(io.context, reg.gas.address, value)) {
                    error = power_error::read_failed;

                    return false;
                }
            }
            else {
                error = power_error::unsupported_address_space;

                return false;
            }

            return true;
        }

        bool write_pm1_control(const pm_register& reg, const pm_io& io, u16 value,
                               power_error& error) {
            if (reg.gas.space == gas_system_io) {
                if (io.write_io == nullptr ||
                    !io.write_io(io.context, static_cast<u16>(reg.gas.address), value)) {
                    error = power_error::write_failed;

                    return false;
                }
            }
            else if (reg.gas.space == gas_system_memory) {
                if (io.write_memory == nullptr ||
                    !io.write_memory(io.context, reg.gas.address, value)) {
                    error = power_error::write_failed;

                    return false;
                }
            }
            else {
                error = power_error::unsupported_address_space;

                return false;
            }

            return true;
        }

        bool enter_sleep(const pm_register& reg, u16 sleep_type, const pm_io& io,
                         power_error& error) {
            u16 current = 0;

            if (!read_pm1_control(reg, io, current, error))
                return false;

            const u16 value =
                static_cast<u16>((current & static_cast<u16>(~pm1_control_mask)) |
                                 (sleep_type << sleep_type_shift) | sleep_enable);

            return write_pm1_control(reg, io, value, error);
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
        case power_error::invalid_reset:
            return "reset register";
        case power_error::unsupported_address_space:
            return "register address space";
        case power_error::unsupported_access_size:
            return "register access size";
        case power_error::read_failed:
            return "PM1 read";
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

        const u8* fadt = tables.fadt.bytes.data();

        const usz length = tables.fadt.bytes.size();

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

        if (!enter_sleep(plan.pm1a, plan.slp_typa, io, error))
            return false;

        if (plan.pm1b.valid && !enter_sleep(plan.pm1b, plan.slp_typb, io, error))
            return false;

        return true;
    }

    bool build_reset_plan(const table_set& tables, reset_plan& out, power_error& error) {
        out   = reset_plan{};
        error = power_error::none;

        if (tables.fadt.bytes.empty()) {
            error = power_error::no_fadt;

            return false;
        }

        const u8* fadt = tables.fadt.bytes.data();

        if (tables.fadt.bytes.size() < fadt_reset_end) {
            error = power_error::short_fadt;

            return false;
        }

        if ((load_u32(fadt + fadt_flags_offset) & fadt_reset_register_supported) == 0) {
            error = power_error::invalid_reset;

            return false;
        }

        const generic_address gas = load_gas(fadt + fadt_reset_register_offset);

        if (gas_is_zero(gas)) {
            error = power_error::invalid_reset;

            return false;
        }

        if (!reset_gas_valid(gas, error))
            return false;

        // PCI Configuration reset registers are spec-valid, but this kernel has
        // no native byte-write contract for that space.  Reject them rather than
        // using a dword read-modify-write that can touch adjacent side-effect bits.
        if (gas.space == gas_pci_configuration) {
            error = power_error::unsupported_address_space;

            return false;
        }

        out.gas   = gas;
        out.value = fadt[fadt_reset_value_offset];
        out.valid = true;

        return true;
    }

    bool execute_reset(const reset_plan& plan, const reset_io& io, power_error& error) {
        error = power_error::none;

        if (!plan.valid) {
            error = power_error::invalid_reset;

            return false;
        }

        if (plan.gas.space == gas_system_io) {
            if (io.write_io == nullptr ||
                !io.write_io(io.context, static_cast<u16>(plan.gas.address),
                             plan.value)) {
                error = power_error::write_failed;

                return false;
            }
        }
        else if (plan.gas.space == gas_system_memory) {
            if (io.write_memory == nullptr ||
                !io.write_memory(io.context, plan.gas.address, plan.value)) {
                error = power_error::write_failed;

                return false;
            }
        }
        else {
            error = power_error::unsupported_address_space;

            return false;
        }

        return true;
    }
} // namespace astia::acpi::detail
