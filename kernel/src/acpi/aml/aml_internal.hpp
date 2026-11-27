#pragma once

#include "../aml.hpp"

namespace astia::acpi::detail {
    constexpr u32 name_segment(char a, char b, char c, char d) {
        return static_cast<u32>(static_cast<u8>(a)) |
               (static_cast<u32>(static_cast<u8>(b)) << 8) |
               (static_cast<u32>(static_cast<u8>(c)) << 16) |
               (static_cast<u32>(static_cast<u8>(d)) << 24);
    }

    constexpr bool lead_name_char(u8 value) {
        return (value >= 'A' && value <= 'Z') || value == '_';
    }

    constexpr bool name_char(u8 value) {
        return lead_name_char(value) || (value >= '0' && value <= '9');
    }

    enum class opcode : u8 {
        zero               = 0x00,
        one                = 0x01,
        alias              = 0x06,
        name               = 0x08,
        byte_prefix        = 0x0A,
        word_prefix        = 0x0B,
        dword_prefix       = 0x0C,
        string_prefix      = 0x0D,
        qword_prefix       = 0x0E,
        scope              = 0x10,
        buffer             = 0x11,
        package            = 0x12,
        var_package        = 0x13,
        method             = 0x14,
        external           = 0x15,
        dual_name          = 0x2E,
        multi_name         = 0x2F,
        local_base         = 0x60,
        arg_base           = 0x68,
        store              = 0x70,
        add                = 0x72,
        concatenate        = 0x73,
        subtract           = 0x74,
        increment          = 0x75,
        decrement          = 0x76,
        multiply           = 0x77,
        divide             = 0x78,
        shift_left         = 0x79,
        shift_right        = 0x7A,
        bit_and            = 0x7B,
        nand               = 0x7C,
        bit_or             = 0x7D,
        nor                = 0x7E,
        bit_xor            = 0x7F,
        bit_not            = 0x80,
        find_left          = 0x81,
        find_right         = 0x82,
        deref              = 0x83,
        mod                = 0x85,
        notify             = 0x86,
        size_of            = 0x87,
        index              = 0x88,
        object_type        = 0x8E,
        l_and              = 0x90,
        l_or               = 0x91,
        l_not              = 0x92,
        l_equal            = 0x93,
        l_greater          = 0x94,
        l_less             = 0x95,
        to_integer         = 0x99,
        copy_object        = 0x9D,
        continue_statement = 0x9F,
        if_statement       = 0xA0,
        else_statement     = 0xA1,
        while_statement    = 0xA2,
        noop               = 0xA3,
        return_statement   = 0xA4,
        break_statement    = 0xA5,
        ones               = 0xFF,
        root_char          = '\\',
        parent_char        = '^',
        extended           = 0x5B,
    };

    constexpr u8 raw(opcode value) {
        return static_cast<u8>(value);
    }

    enum class extended_opcode : u8 {
        mutex             = 0x01,
        event             = 0x02,
        revision          = 0x30,
        debug             = 0x31,
        fatal             = 0x32,
        operation_region  = 0x80,
        field             = 0x81,
        device            = 0x82,
        processor         = 0x83,
        power_resource    = 0x84,
        thermal_zone      = 0x85,
        index_field       = 0x86,
        bank_field        = 0x87,
        data_table_region = 0x88,
    };

    constexpr u8 raw(extended_opcode value) {
        return static_cast<u8>(value);
    }

    inline constexpr u64 interpreter_revision = 0x20230628ull;
} // namespace astia::acpi::detail
