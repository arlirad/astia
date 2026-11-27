#include "aml_internal.hpp"

namespace astia::acpi::detail {
    bool interpreter::parse_alias(cursor& c, node_id scope) {
        name_ref source;
        name_ref alias;

        if (!read_name_ref(c, source) || !read_name_ref(c, alias))
            return false;

        const node_id target  = resolve_lookup(source, scope);
        const node_id created = resolve_definition(alias, scope, true);

        if (created == no_node)
            return fail(aml_error::object_limit);

        _nodes[created].kind   = static_cast<u8>(node_kind::alias);
        _nodes[created].target = target;

        return true;
    }

    bool interpreter::parse_bank_field(cursor& c, node_id scope) {
        const u8* package_end = open_package(c);
        if (package_end == nullptr)
            return false;

        name_ref region;
        name_ref bank;

        if (!read_name_ref(c, region) || !read_name_ref(c, bank))
            return false;

        u64 ignored = 0;

        if (!eval_constant(c, scope, ignored))
            return false;

        u8 flags = 0;

        if (!read_u8(c, flags))
            return false;

        return parse_field_list(c, package_end, scope) && close_package(c, package_end);
    }

    bool interpreter::parse_buffer(cursor& c, value_id& out) {
        if (at_end(c) || *c.position != raw(opcode::buffer))
            return fail(aml_error::truncated);

        ++c.position;

        const u8* package_end = open_package(c);
        if (package_end == nullptr)
            return false;

        u64 size = 0;

        if (!eval_constant(c, root(), size))
            return false;

        const u64 remaining = static_cast<u64>(package_end - c.position);

        if (size > remaining)
            return fail(aml_error::package_length);

        const value_id id = add_value(value_kind::buffer);
        if (id == no_value)
            return fail(aml_error::value_limit);

        _values[id].data   = c.position;
        _values[id].length = static_cast<u32>(size);
        c.position         = package_end;
        out                = id;

        return close_package(c, package_end);
    }

    bool interpreter::parse_data_object(cursor& c, value_id& out) {
        term_guard depth{*this};

        if (!depth)
            return false;

        if (at_end(c))
            return fail(aml_error::truncated);

        if (*c.position == raw(opcode::package))
            return parse_package(c, out, false);

        if (*c.position == raw(opcode::var_package))
            return parse_package(c, out, true);

        if (*c.position == raw(opcode::buffer))
            return parse_buffer(c, out);

        const auto op = static_cast<opcode>(*c.position);

        switch (op) {
        case opcode::zero:
            ++c.position;

            out = add_integer(0);

            break;
        case opcode::one:
            ++c.position;

            out = add_integer(1);

            break;
        case opcode::ones:
            ++c.position;

            out = add_integer(~u64{0});

            break;
        case opcode::byte_prefix:
        case opcode::word_prefix:
        case opcode::dword_prefix:
        case opcode::qword_prefix: {
            ++c.position;

            u64 value = 0;

            if (op == opcode::byte_prefix) {
                u8 byte = 0;

                if (!read_u8(c, byte))
                    return false;

                value = byte;
            }
            else if (op == opcode::word_prefix) {
                u16 word = 0;

                if (!read_u16(c, word))
                    return false;

                value = word;
            }
            else if (op == opcode::dword_prefix) {
                u32 dword = 0;

                if (!read_u32(c, dword))
                    return false;

                value = dword;
            }
            else if (!read_u64(c, value)) {
                return false;
            }

            out = add_integer(value);

            break;
        }
        case opcode::string_prefix: {
            ++c.position;

            const u8* const start = c.position;

            while (c.position < c.end && *c.position != 0) {
                ++c.position;
            }

            if (c.position >= c.end)
                return fail(aml_error::truncated);

            const value_id id = add_value(value_kind::string);
            if (id == no_value)
                return fail(aml_error::value_limit);

            _values[id].data   = start;
            _values[id].length = static_cast<u32>(c.position - start);

            ++c.position;

            out = id;

            break;
        }
        case opcode::extended:
            if (static_cast<usz>(c.end - c.position) < 2)
                return fail(aml_error::truncated);

            if (c.position[1] == raw(extended_opcode::revision)) {
                c.position += 2;
                out = add_integer(interpreter_revision);

                break;
            }

            return fail(aml_error::unsupported_object);
        default: {
            if (op == opcode::root_char || op == opcode::parent_char ||
                op == opcode::dual_name || op == opcode::multi_name ||
                lead_name_char(raw(op))) {
                name_ref reference;

                if (!read_name_ref(c, reference))
                    return false;

                out = add_name_reference(reference);
                if (out == no_value)
                    return fail(aml_error::reference_limit);

                return true;
            }

            return fail(aml_error::unsupported_object);
        }
        }

        if (out == no_value)
            return fail(aml_error::value_limit);

        return true;
    }

    bool interpreter::parse_data_table_region(cursor& c, node_id scope) {
        name_ref reference;

        if (!read_name_ref(c, reference))
            return false;

        for (u32 i = 0; i < 3; ++i) {
            if (c.position >= c.end)
                return fail(aml_error::truncated);

            if (*c.position++ != raw(opcode::string_prefix))
                return fail(aml_error::invalid_value);

            while (c.position < c.end && *c.position != 0) {
                ++c.position;
            }

            if (c.position >= c.end)
                return fail(aml_error::truncated);

            ++c.position;
        }

        const node_id target = resolve_definition(reference, scope, true);
        if (target == no_node)
            return fail(aml_error::object_limit);

        _nodes[target].kind = static_cast<u8>(node_kind::region);

        return true;
    }

    bool interpreter::parse_definition(cursor& c, node_id scope) {
        term_guard depth{*this};

        if (!depth)
            return false;

        if (at_end(c))
            return true;

        const auto op = static_cast<opcode>(*c.position);

        switch (op) {
        case opcode::alias:
            ++c.position;

            return parse_alias(c, scope);
        case opcode::name:
            ++c.position;

            return parse_name_object(c, scope);
        case opcode::scope:
            ++c.position;

            return parse_scope_object(c, scope, node_kind::scope);
        case opcode::method:
            ++c.position;

            return parse_method(c, scope);
        case opcode::external:
            ++c.position;

            return parse_external(c, scope);
        case opcode::extended: {
            if (static_cast<usz>(c.end - c.position) < 2)
                return fail(aml_error::truncated);

            const auto extended = static_cast<extended_opcode>(c.position[1]);

            c.position += 2;

            switch (extended) {
            case extended_opcode::mutex:
                return parse_mutex(c, scope);
            case extended_opcode::event:
                return parse_event(c, scope);
            case extended_opcode::operation_region:
                return parse_operation_region(c, scope);
            case extended_opcode::field:
                return parse_field(c, scope);
            case extended_opcode::device:
                return parse_scope_object(c, scope, node_kind::device);
            case extended_opcode::processor:
                return parse_scope_object(c, scope, node_kind::processor);
            case extended_opcode::power_resource:
                return parse_scope_object(c, scope, node_kind::power_resource);
            case extended_opcode::thermal_zone:
                return parse_scope_object(c, scope, node_kind::thermal_zone);
            case extended_opcode::index_field:
                return parse_index_field(c, scope);
            case extended_opcode::bank_field:
                return parse_bank_field(c, scope);
            case extended_opcode::data_table_region:
                return parse_data_table_region(c, scope);
            default:
                return fail(aml_error::unsupported_opcode);
            }
        }
        default:
            return fail(aml_error::unsupported_opcode);
        }
    }

    bool interpreter::parse_event(cursor& c, node_id scope) {
        name_ref reference;

        if (!read_name_ref(c, reference))
            return false;

        const node_id target = resolve_definition(reference, scope, true);
        if (target == no_node)
            return fail(aml_error::object_limit);

        _nodes[target].kind = static_cast<u8>(node_kind::event);

        return true;
    }

    bool interpreter::parse_external(cursor& c, node_id scope) {
        name_ref reference;

        if (!read_name_ref(c, reference))
            return false;

        u8 object_type    = 0;
        u8 argument_count = 0;

        if (!read_u8(c, object_type) || !read_u8(c, argument_count))
            return false;

        node_id target = resolve_lookup(reference, scope);
        if (target == no_node)
            target = resolve_definition(reference, scope, true);

        if (target == no_node)
            return fail(aml_error::object_limit);

        if (_nodes[target].kind == static_cast<u8>(node_kind::scope)) {
            _nodes[target].kind        = static_cast<u8>(node_kind::external);
            _nodes[target].arg_count   = argument_count;
            _nodes[target].object_type = object_type;
        }

        return true;
    }

    bool interpreter::parse_field(cursor& c, node_id scope) {
        const u8* package_end = open_package(c);
        if (package_end == nullptr)
            return false;

        name_ref region;

        if (!read_name_ref(c, region))
            return false;

        u8 flags = 0;

        if (!read_u8(c, flags))
            return false;

        return parse_field_list(c, package_end, scope) && close_package(c, package_end);
    }

    bool interpreter::parse_field_list(cursor& c, const u8* end, node_id scope) {
        if (c.position == nullptr || end == nullptr)
            return fail(aml_error::truncated);

        while (c.position < end) {
            const u8 tag = *c.position++;
            if (tag == 0x00) {
                u32 bits  = 0;
                u32 bytes = 0;

                if (!read_package_length(c, bits, bytes))
                    return false;

                continue;
            }

            if (tag == 0x01) {
                u8 access_type      = 0;
                u8 access_attribute = 0;

                if (!read_u8(c, access_type) || !read_u8(c, access_attribute))
                    return false;

                continue;
            }

            if (tag == 0x02) {
                if (c.position >= end)
                    return fail(aml_error::truncated);

                if (*c.position == raw(opcode::buffer)) {
                    value_id ignored = no_value;

                    if (!parse_buffer(c, ignored))
                        return false;
                }
                else {
                    name_ref ignored;

                    if (!read_name_ref(c, ignored))
                        return false;
                }

                continue;
            }

            if (tag == 0x03) {
                u8 access_type      = 0;
                u8 access_attribute = 0;
                u8 access_length    = 0;

                if (!read_u8(c, access_type) || !read_u8(c, access_attribute) ||
                    !read_u8(c, access_length))
                    return false;

                continue;
            }

            c.position--;

            u32 segment = 0;

            if (!read_name_segment(c, segment))
                return false;

            u32 bits  = 0;
            u32 bytes = 0;

            if (!read_package_length(c, bits, bytes))
                return false;

            if (add_node(scope, segment, node_kind::field) == no_node)
                return fail(aml_error::object_limit);
        }

        return c.position == end;
    }

    bool interpreter::parse_index_field(cursor& c, node_id scope) {
        const u8* package_end = open_package(c);
        if (package_end == nullptr)
            return false;

        name_ref index;
        name_ref data;

        if (!read_name_ref(c, index) || !read_name_ref(c, data))
            return false;

        u8 flags = 0;

        if (!read_u8(c, flags))
            return false;

        return parse_field_list(c, package_end, scope) && close_package(c, package_end);
    }

    bool interpreter::parse_method(cursor& c, node_id scope) {
        const u8* package_end = open_package(c);
        if (package_end == nullptr)
            return false;

        name_ref reference;

        if (!read_name_ref(c, reference))
            return false;

        u8 flags = 0;

        if (!read_u8(c, flags))
            return false;

        const node_id target = resolve_definition(reference, scope, true);
        if (target == no_node)
            return fail(aml_error::object_limit);

        node& entry = _nodes[target];

        entry.kind        = static_cast<u8>(node_kind::method);
        entry.arg_count   = flags & 0x07;
        entry.flags       = flags;
        entry.body        = c.position;
        entry.body_length = static_cast<u32>(package_end - c.position);

        c.position = package_end;

        return close_package(c, package_end);
    }

    bool interpreter::parse_mutex(cursor& c, node_id scope) {
        name_ref reference;

        if (!read_name_ref(c, reference))
            return false;

        u8 flags = 0;

        if (!read_u8(c, flags))
            return false;

        const node_id target = resolve_definition(reference, scope, true);
        if (target == no_node)
            return fail(aml_error::object_limit);

        _nodes[target].kind  = static_cast<u8>(node_kind::mutex);
        _nodes[target].flags = flags;

        return true;
    }

    bool interpreter::parse_name_object(cursor& c, node_id scope) {
        name_ref reference;

        if (!read_name_ref(c, reference))
            return false;

        const node_id target = resolve_definition(reference, scope, true);
        if (target == no_node)
            return fail(aml_error::object_limit);

        value_id data = no_value;

        if (!parse_data_object(c, data))
            return false;

        node& entry = _nodes[target];

        entry.kind  = static_cast<u8>(node_kind::name);
        entry.value = data;

        return true;
    }

    bool interpreter::parse_operation_region(cursor& c, node_id scope) {
        name_ref reference;

        if (!read_name_ref(c, reference))
            return false;

        u8 space = 0;

        if (!read_u8(c, space))
            return false;

        u64 offset = 0;
        u64 length = 0;

        if (!eval_constant(c, scope, offset) || !eval_constant(c, scope, length))
            return false;

        const node_id target = resolve_definition(reference, scope, true);
        if (target == no_node)
            return fail(aml_error::object_limit);

        node& entry = _nodes[target];

        entry.kind        = static_cast<u8>(node_kind::region);
        entry.flags       = space;
        entry.target      = no_node;
        entry.body        = nullptr;
        entry.body_length = 0;

        return true;
    }

    bool interpreter::parse_package(cursor& c, value_id& out, bool variable) {
        if (at_end(c) ||
            *c.position != (variable ? raw(opcode::var_package) : raw(opcode::package)))
            return fail(aml_error::truncated);

        ++c.position;

        const u8* package_end = open_package(c);
        if (package_end == nullptr)
            return false;

        u32 count = 0;

        if (variable) {
            u64 value = 0;

            if (!eval_constant(c, root(), value))
                return false;

            if (value > max_aml_package_items)
                return fail(aml_error::value_limit);

            count = static_cast<u32>(value);
        }
        else {
            u8 fixed = 0;

            if (!read_u8(c, fixed))
                return false;

            count = fixed;
        }

        const value_id package = add_value(value_kind::package);
        if (package == no_value)
            return fail(aml_error::value_limit);

        for (u32 i = 0; i < count; ++i) {
            value_id item = no_value;

            if (!parse_data_object(c, item))
                return false;

            if (!append_package_item(package, item))
                return false;
        }

        if (c.position != package_end)
            return fail(aml_error::package_length);

        out = package;

        return close_package(c, package_end);
    }

    bool interpreter::parse_scope(const table_set& tables) {
        if (tables.dsdt.bytes.size() > sdt_header_size) {
            if (tables.dsdt.bytes.data() == nullptr)
                return fail(aml_error::truncated);

            cursor c{
                tables.dsdt.bytes.data() + sdt_header_size,
                tables.dsdt.bytes.data() + tables.dsdt.bytes.size(),
            };

            if (!parse_term_list(c, root()))
                return false;
        }

        for (usz i = 0; i < tables.ssdt_count; ++i) {
            const table& ssdt = tables.ssdt[i];

            if (ssdt.bytes.size() <= sdt_header_size || ssdt.bytes.data() == nullptr)
                return fail(aml_error::truncated);

            cursor c{
                ssdt.bytes.data() + sdt_header_size,
                ssdt.bytes.data() + ssdt.bytes.size(),
            };

            if (!parse_term_list(c, root()))
                return false;
        }

        return true;
    }

    bool interpreter::parse_scope_object(cursor& c, node_id scope, node_kind kind,
                                         u8 arg_count, u32 object_type) {
        const u8* package_end = open_package(c);
        if (package_end == nullptr)
            return false;

        name_ref reference;

        if (!read_name_ref(c, reference))
            return false;

        const node_id target = resolve_definition(reference, scope, true);
        if (target == no_node)
            return fail(aml_error::object_limit);

        node& entry = _nodes[target];

        entry.kind        = static_cast<u8>(kind);
        entry.arg_count   = arg_count;
        entry.object_type = static_cast<u8>(object_type);

        u8  processor_id   = 0;
        u8  pblk_length    = 0;
        u32 pblk_address   = 0;
        u8  system_level   = 0;
        u16 resource_order = 0;

        if (kind == node_kind::processor) {
            if (!read_u8(c, processor_id) || !read_u32(c, pblk_address) ||
                !read_u8(c, pblk_length))
                return false;
        }
        else if (kind == node_kind::power_resource) {
            if (!read_u8(c, system_level) || !read_u16(c, resource_order))
                return false;
        }

        (void) processor_id;
        (void) pblk_address;
        (void) pblk_length;
        (void) system_level;
        (void) resource_order;

        if (!parse_term_list(c, target))
            return false;

        return close_package(c, package_end);
    }

    bool interpreter::parse_term_list(cursor& c, node_id scope) {
        while (!at_end(c)) {
            if (!step())
                return false;

            if (!parse_definition(c, scope))
                return false;
        }

        return true;
    }
} // namespace astia::acpi::detail
