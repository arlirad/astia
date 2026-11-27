#include "interpreter.hpp"
#include "name.hpp"

namespace astia::acpi::detail {
    bool interpreter::read_u8(cursor& c, u8& value) {
        if (c.position == nullptr || c.position >= c.end)
            return fail(aml_error::truncated);

        value = *c.position++;

        return true;
    }

    bool interpreter::read_u16(cursor& c, u16& value) {
        if (c.position == nullptr || static_cast<usize>(c.end - c.position) < 2)
            return fail(aml_error::truncated);

        value = load_u16(c.position);
        c.position += 2;

        return true;
    }

    bool interpreter::read_u32(cursor& c, u32& value) {
        if (c.position == nullptr || static_cast<usize>(c.end - c.position) < 4)
            return fail(aml_error::truncated);

        value = load_u32(c.position);
        c.position += 4;

        return true;
    }

    bool interpreter::read_u64(cursor& c, u64& value) {
        if (c.position == nullptr || static_cast<usize>(c.end - c.position) < 8)
            return fail(aml_error::truncated);

        value = load_u64(c.position);
        c.position += 8;

        return true;
    }

    bool interpreter::read_package_length(cursor& c, u32& value, u32& bytes) {
        u8 first = 0;

        if (!read_u8(c, first))
            return false;

        const u8 form = static_cast<u8>(first >> 6);
        if (form == 0) {
            value = first & 0x3Fu;
            bytes = 1;

            return value != 0 || fail(aml_error::package_length);
        }

        bytes = static_cast<u32>(form) + 1;

        if (static_cast<usize>(c.end - c.position) < bytes - 1)
            return fail(aml_error::truncated);

        value = first & 0x0Fu;

        for (u32 i = 1; i < bytes; ++i) {
            value |= static_cast<u32>(*c.position++) << (4 + (i - 1) * 8);
        }

        if (value < bytes)
            return fail(aml_error::package_length);

        return true;
    }

    const u8* interpreter::open_package(cursor& c) {
        const u8* const start = c.position;

        u32 length = 0;
        u32 bytes  = 0;

        if (!read_package_length(c, length, bytes))
            return nullptr;

        if (static_cast<u64>(c.end - start) < length) {
            fail(aml_error::truncated);

            return nullptr;
        }

        if (_package_depth == max_aml_package_depth) {
            fail(aml_error::package_length);

            return nullptr;
        }

        const u8* const end = start + length;

        _package_ends[_package_depth++] = c.end;
        c.end                           = end;

        return end;
    }

    bool interpreter::close_package(cursor& c, const u8* end) {
        if (c.position != end)
            return fail(aml_error::package_length);

        if (_package_depth == 0)
            return fail(aml_error::package_length);

        c.position = end;
        c.end      = _package_ends[--_package_depth];

        return true;
    }

    bool interpreter::read_name_segment(cursor& c, u32& segment) {
        if (c.position == nullptr || static_cast<usize>(c.end - c.position) < 4)
            return fail(aml_error::truncated);

        if (!lead_name_char(c.position[0]) || !name_char(c.position[1]) ||
            !name_char(c.position[2]) || !name_char(c.position[3]))
            return fail(aml_error::name);

        segment = load_u32(c.position);
        c.position += 4;

        return true;
    }

    bool interpreter::read_name_ref(cursor& c, name_ref& reference) {
        reference = name_ref{};

        while (c.position < c.end &&
               static_cast<opcode>(*c.position) == opcode::parent_char) {
            if (reference.parent_prefix == 255)
                return fail(aml_error::name);

            ++reference.parent_prefix;
            ++c.position;
        }

        if (c.position < c.end && static_cast<opcode>(*c.position) == opcode::root_char) {
            reference.root = true;

            ++c.position;
        }

        if (c.position >= c.end)
            return fail(aml_error::truncated);

        const opcode op = static_cast<opcode>(*c.position);

        if (op == opcode::zero) {
            ++c.position;

            return true;
        }

        if (op == opcode::dual_name_prefix) {
            ++c.position;

            if (static_cast<usize>(reference.count) + 2 > max_aml_name_segments)
                return fail(aml_error::name);

            for (u32 i = 0; i < 2; ++i) {
                if (!read_name_segment(c, reference.segment[reference.count++]))
                    return false;
            }

            return true;
        }

        if (op == opcode::multi_name_prefix) {
            ++c.position;

            u8 count = 0;

            if (!read_u8(c, count))
                return false;

            if (count == 0 || count > max_aml_name_segments)
                return fail(aml_error::name);

            for (u8 i = 0; i < count; ++i) {
                if (!read_name_segment(c, reference.segment[reference.count++]))
                    return false;
            }

            return true;
        }

        return read_name_segment(c, reference.segment[reference.count++]);
    }
} // namespace astia::acpi::detail
