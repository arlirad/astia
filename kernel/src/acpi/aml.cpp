#include "aml.hpp"

namespace astia::acpi::detail {
    namespace {
        constexpr u32 name_segment(char a, char b, char c, char d) {
            return static_cast<u32>(static_cast<u8>(a)) |
                   (static_cast<u32>(static_cast<u8>(b)) << 8) |
                   (static_cast<u32>(static_cast<u8>(c)) << 16) |
                   (static_cast<u32>(static_cast<u8>(d)) << 24);
        }

        bool lead_name_char(u8 value) {
            return (value >= 'A' && value <= 'Z') || value == '_';
        }

        bool name_char(u8 value) {
            return lead_name_char(value) || (value >= '0' && value <= '9');
        }

        constexpr u8 op_zero          = 0x00;
        constexpr u8 op_one           = 0x01;
        constexpr u8 op_alias         = 0x06;
        constexpr u8 op_name          = 0x08;
        constexpr u8 op_byte_prefix   = 0x0A;
        constexpr u8 op_word_prefix   = 0x0B;
        constexpr u8 op_dword_prefix  = 0x0C;
        constexpr u8 op_string_prefix = 0x0D;
        constexpr u8 op_qword_prefix  = 0x0E;
        constexpr u8 op_scope         = 0x10;
        constexpr u8 op_buffer        = 0x11;
        constexpr u8 op_package       = 0x12;
        constexpr u8 op_var_package   = 0x13;
        constexpr u8 op_method        = 0x14;
        constexpr u8 op_external      = 0x15;
        constexpr u8 op_dual_name     = 0x2E;
        constexpr u8 op_multi_name    = 0x2F;
        constexpr u8 op_root_char     = '\\';
        constexpr u8 op_parent_char   = '^';
        constexpr u8 op_local_base    = 0x60;
        constexpr u8 op_arg_base      = 0x68;
        constexpr u8 op_store         = 0x70;
        constexpr u8 op_add           = 0x72;
        constexpr u8 op_concatenate   = 0x73;
        constexpr u8 op_subtract      = 0x74;
        constexpr u8 op_increment     = 0x75;
        constexpr u8 op_decrement     = 0x76;
        constexpr u8 op_multiply      = 0x77;
        constexpr u8 op_divide        = 0x78;
        constexpr u8 op_shift_left    = 0x79;
        constexpr u8 op_shift_right   = 0x7A;
        constexpr u8 op_and           = 0x7B;
        constexpr u8 op_nand          = 0x7C;
        constexpr u8 op_or            = 0x7D;
        constexpr u8 op_nor           = 0x7E;
        constexpr u8 op_xor           = 0x7F;
        constexpr u8 op_not           = 0x80;
        constexpr u8 op_find_left     = 0x81;
        constexpr u8 op_find_right    = 0x82;
        constexpr u8 op_deref         = 0x83;
        constexpr u8 op_concat_res    = 0x84;
        constexpr u8 op_mod           = 0x85;
        constexpr u8 op_notify        = 0x86;
        constexpr u8 op_size_of       = 0x87;
        constexpr u8 op_index         = 0x88;
        constexpr u8 op_match         = 0x89;
        constexpr u8 op_create_dword  = 0x8A;
        constexpr u8 op_create_word   = 0x8B;
        constexpr u8 op_create_byte   = 0x8C;
        constexpr u8 op_create_bit    = 0x8D;
        constexpr u8 op_object_type   = 0x8E;
        constexpr u8 op_create_qword  = 0x8F;
        constexpr u8 op_l_and         = 0x90;
        constexpr u8 op_l_or          = 0x91;
        constexpr u8 op_l_not         = 0x92;
        constexpr u8 op_l_equal       = 0x93;
        constexpr u8 op_l_greater     = 0x94;
        constexpr u8 op_l_less        = 0x95;
        constexpr u8 op_to_buffer     = 0x96;
        constexpr u8 op_to_decimal    = 0x97;
        constexpr u8 op_to_hex        = 0x98;
        constexpr u8 op_to_integer    = 0x99;
        constexpr u8 op_to_string     = 0x9C;
        constexpr u8 op_copy_object   = 0x9D;
        constexpr u8 op_mid           = 0x9E;
        constexpr u8 op_continue      = 0x9F;
        constexpr u8 op_if            = 0xA0;
        constexpr u8 op_else          = 0xA1;
        constexpr u8 op_while         = 0xA2;
        constexpr u8 op_noop          = 0xA3;
        constexpr u8 op_return        = 0xA4;
        constexpr u8 op_break         = 0xA5;
        constexpr u8 op_ones          = 0xFF;
        constexpr u8 op_extended      = 0x5B;

        constexpr u8 ext_mutex             = 0x01;
        constexpr u8 ext_event             = 0x02;
        constexpr u8 ext_revision          = 0x30;
        constexpr u8 ext_debug             = 0x31;
        constexpr u8 ext_fatal             = 0x32;
        constexpr u8 ext_operation_region  = 0x80;
        constexpr u8 ext_field             = 0x81;
        constexpr u8 ext_device            = 0x82;
        constexpr u8 ext_processor         = 0x83;
        constexpr u8 ext_power_resource    = 0x84;
        constexpr u8 ext_thermal_zone      = 0x85;
        constexpr u8 ext_index_field       = 0x86;
        constexpr u8 ext_bank_field        = 0x87;
        constexpr u8 ext_data_table_region = 0x88;

        constexpr u64 interpreter_revision = 0x20230628ull;
    } // namespace

    const char* aml_error_text(aml_error error) {
        switch (error) {
        case aml_error::none:
            return "ok";
        case aml_error::truncated:
            return "truncated AML";
        case aml_error::package_length:
            return "AML package length";
        case aml_error::name:
            return "AML name";
        case aml_error::unsupported_opcode:
            return "unsupported AML opcode";
        case aml_error::unsupported_object:
            return "unsupported AML object";
        case aml_error::object_limit:
            return "AML object limit";
        case aml_error::value_limit:
            return "AML value limit";
        case aml_error::reference_limit:
            return "AML reference limit";
        case aml_error::recursion_limit:
            return "AML recursion limit";
        case aml_error::execution_limit:
            return "AML execution limit";
        case aml_error::missing_object:
            return "AML missing object";
        case aml_error::wrong_type:
            return "AML wrong type";
        case aml_error::invalid_value:
            return "AML invalid value";
        case aml_error::method_arguments:
            return "AML method arguments";
        }

        return "unknown";
    }

    bool interpreter::fail(aml_error error) {
        if (_error == aml_error::none)
            _error = error;

        return false;
    }

    bool interpreter::at_end(const cursor& c) const {
        return c.position >= c.end;
    }

    bool interpreter::enter_term() {
        if (_term_depth == max_aml_term_depth)
            return fail(aml_error::recursion_limit);

        ++_term_depth;

        return true;
    }

    void interpreter::leave_term() {
        if (_term_depth != 0)
            --_term_depth;
    }

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

    bool interpreter::open_package(cursor& c, const u8*& end) {
        const u8* const start  = c.position;
        u32             length = 0;
        u32             bytes  = 0;

        if (!read_package_length(c, length, bytes))
            return false;

        if (static_cast<u64>(c.end - start) < length)
            return fail(aml_error::truncated);

        if (_package_depth == max_aml_package_depth)
            return fail(aml_error::package_length);

        end                             = start + length;
        _package_ends[_package_depth++] = c.end;
        c.end                           = end;

        return true;
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

        while (c.position < c.end && *c.position == op_parent_char) {
            if (reference.parent_prefix == 255)
                return fail(aml_error::name);

            ++reference.parent_prefix;
            ++c.position;
        }

        if (c.position < c.end && *c.position == op_root_char) {
            reference.root = true;
            ++c.position;
        }

        if (c.position >= c.end)
            return fail(aml_error::truncated);

        if (*c.position == op_zero) {
            ++c.position;

            return true;
        }

        const u8 prefix = *c.position;

        if (prefix == op_dual_name) {
            ++c.position;

            if (static_cast<usize>(reference.count) + 2 > max_aml_name_segments)
                return fail(aml_error::name);

            for (u32 i = 0; i < 2; ++i) {
                if (!read_name_segment(c, reference.segment[reference.count++]))
                    return false;
            }

            return true;
        }

        if (prefix == op_multi_name) {
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

    node_id interpreter::add_node(node_id parent, u32 name, node_kind kind) {
        if (_node_count == max_aml_nodes || parent >= _node_count)
            return no_node;

        const node_id id = static_cast<node_id>(_node_count++);

        node& added = _nodes[id];

        added                      = node{};
        added.name                 = name;
        added.parent               = parent;
        added.kind                 = static_cast<u8>(kind);
        added.value                = no_value;
        added.target               = no_node;
        added.next_sibling         = _nodes[parent].first_child;
        _nodes[parent].first_child = id;

        return id;
    }

    node_id interpreter::find_child(node_id parent, u32 name) const {
        if (parent >= _node_count)
            return no_node;

        for (node_id child = _nodes[parent].first_child; child != no_node;
             child         = _nodes[child].next_sibling) {
            if (_nodes[child].name == name)
                return child;
        }

        return no_node;
    }

    node_id interpreter::resolve_lookup(const name_ref& reference, node_id scope) const {
        if (scope >= _node_count)
            return no_node;

        node_id base = reference.root ? root() : scope;

        for (u8 i = 0; i < reference.parent_prefix; ++i) {
            if (base == root())
                break;

            base = _nodes[base].parent;
        }

        if (reference.count == 0)
            return base;

        const bool upward = !reference.root && reference.parent_prefix == 0;
        node_id    start  = base;

        while (true) {
            node_id current = start;
            bool    found   = true;

            for (u8 i = 0; i < reference.count; ++i) {
                current = find_child(current, reference.segment[i]);

                if (current == no_node) {
                    found = false;

                    break;
                }
            }

            if (found)
                return current;

            if (!upward || start == root())
                break;

            start = _nodes[start].parent;
        }

        return no_node;
    }

    node_id interpreter::resolve_definition(const name_ref& reference, node_id scope,
                                            bool create_parents) {
        if (scope >= _node_count)
            return no_node;

        node_id base = reference.root ? root() : scope;

        for (u8 i = 0; i < reference.parent_prefix; ++i) {
            if (base == root())
                break;

            base = _nodes[base].parent;
        }

        if (reference.count == 0)
            return base;

        for (u8 i = 0; i + 1 < reference.count; ++i) {
            node_id next = find_child(base, reference.segment[i]);

            if (next == no_node) {
                if (!create_parents)
                    return no_node;

                next = add_node(base, reference.segment[i], node_kind::scope);

                if (next == no_node)
                    return no_node;
            }

            base = next;
        }

        node_id existing = find_child(base, reference.segment[reference.count - 1]);

        if (existing != no_node)
            return existing;

        return add_node(base, reference.segment[reference.count - 1], node_kind::scope);
    }

    node_id interpreter::follow_alias(node_id id, u32 depth) {
        if (id >= _node_count || depth == max_aml_reference_depth)
            return no_node;

        const node& entry = _nodes[id];

        if (entry.kind != static_cast<u8>(node_kind::alias))
            return id;

        if (entry.target == no_node)
            return no_node;

        return follow_alias(entry.target, depth + 1);
    }

    value_id interpreter::add_value(value_kind kind) {
        if (_value_count == max_aml_values)
            return no_value;

        const value_id id = static_cast<value_id>(_value_count++);

        _values[id]           = value{};
        _values[id].kind      = static_cast<u8>(kind);
        _values[id].first     = no_value;
        _values[id].last      = no_value;
        _values[id].next      = no_value;
        _values[id].reference = no_value;

        return id;
    }

    value_id interpreter::add_integer(u64 value) {
        const value_id id = add_value(value_kind::integer);

        if (id != no_value)
            _values[id].integer = value;

        return id;
    }

    value_id interpreter::add_name_reference(const name_ref& reference) {
        if (_reference_count == max_aml_name_refs)
            return no_value;

        const value_id id = add_value(value_kind::name_reference);

        if (id == no_value)
            return no_value;

        _references[_reference_count] = reference;
        _values[id].reference         = static_cast<u32>(_reference_count++);

        return id;
    }

    bool interpreter::append_package_item(value_id package, value_id item) {
        if (package >= _value_count)
            return fail(aml_error::wrong_type);

        value& target = _values[package];

        if (target.kind != static_cast<u8>(value_kind::package))
            return fail(aml_error::wrong_type);

        if (target.count == max_aml_package_items)
            return fail(aml_error::value_limit);

        if (item >= _value_count)
            return fail(aml_error::value_limit);

        _values[item].next = no_value;

        if (target.first == no_value)
            target.first = item;
        else
            _values[target.last].next = item;

        target.last = item;
        ++target.count;

        return true;
    }

    value_id interpreter::dereference(value_id id) {
        return dereference(id, 0);
    }

    value_id interpreter::dereference(value_id id, u32 depth) {
        if (id >= _value_count || depth == max_aml_reference_depth)
            return no_value;

        const value& entry = _values[id];

        if (entry.kind != static_cast<u8>(value_kind::name_reference))
            return id;

        if (entry.reference >= _reference_count)
            return no_value;

        const name_ref& reference = _references[entry.reference];
        const node_id   resolved  = resolve_lookup(reference, root());

        if (resolved == no_node ||
            (_nodes[resolved].kind != static_cast<u8>(node_kind::alias) &&
             _nodes[resolved].kind != static_cast<u8>(node_kind::name)))
            return no_value;

        const node_id target_id = follow_alias(resolved, depth);

        if (target_id == no_node)
            return no_value;

        const node& target = _nodes[target_id];

        if (target.value == no_value)
            return no_value;

        return dereference(target.value, depth + 1);
    }

    bool interpreter::as_integer(value_id id, u64& out) {
        const value_id resolved = dereference(id);

        if (resolved == no_value ||
            _values[resolved].kind != static_cast<u8>(value_kind::integer))
            return false;

        out = _values[resolved].integer;

        return true;
    }

    bool interpreter::as_package(value_id id, const value*& out) {
        const value_id resolved = dereference(id);

        if (resolved == no_value ||
            _values[resolved].kind != static_cast<u8>(value_kind::package))
            return false;

        out = &_values[resolved];

        return true;
    }

    bool interpreter::build(const table_set& tables) {
        _node_count      = 0;
        _value_count     = 0;
        _reference_count = 0;
        _error           = aml_error::none;
        _steps           = 0;
        _call_depth      = 0;
        _term_depth      = 0;
        _package_depth   = 0;

        const node_id base = 0;

        _nodes[base]              = node{};
        _nodes[base].parent       = no_node;
        _nodes[base].kind         = static_cast<u8>(node_kind::root);
        _nodes[base].value        = no_value;
        _nodes[base].target       = no_node;
        _nodes[base].first_child  = no_node;
        _nodes[base].next_sibling = no_node;
        _node_count               = 1;

        constexpr u32 predefined[] = {
            name_segment('_', 'S', 'B', '_'), name_segment('_', 'G', 'P', 'E'),
            name_segment('_', 'P', 'R', '_'), name_segment('_', 'T', 'Z', '_'),
            name_segment('_', 'S', 'I', '_'), name_segment('_', 'G', 'L', '_'),
        };

        for (u32 name : predefined) {
            if (add_node(base, name, node_kind::scope) == no_node)
                return fail(aml_error::object_limit);
        }

        if (tables.dsdt.bytes.size() <= sdt_header_size)
            return fail(aml_error::truncated);

        if (!parse_scope(tables))
            return false;

        return _error == aml_error::none;
    }

    bool interpreter::parse_scope(const table_set& tables) {
        if (tables.dsdt.bytes.size() > sdt_header_size) {
            cursor c{
                tables.dsdt.bytes.data() + sdt_header_size,
                tables.dsdt.bytes.data() + tables.dsdt.bytes.size(),
            };

            if (!parse_term_list(c, root()))
                return false;
        }

        for (usize i = 0; i < tables.ssdt_count; ++i) {
            const table& ssdt = tables.ssdt[i];

            if (ssdt.bytes.size() <= sdt_header_size)
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

    bool interpreter::parse_term_list(cursor& c, node_id scope) {
        while (!at_end(c)) {
            if (!step())
                return false;

            if (!parse_definition(c, scope))
                return false;
        }

        return true;
    }

    bool interpreter::parse_definition(cursor& c, node_id scope) {
        term_guard depth{*this};

        if (!depth)
            return false;

        if (at_end(c))
            return true;

        const u8 opcode = *c.position;

        switch (opcode) {
        case op_alias:
            ++c.position;

            return parse_alias(c, scope);
        case op_name:
            ++c.position;

            return parse_name_object(c, scope);
        case op_scope:
            ++c.position;

            return parse_scope_object(c, scope, node_kind::scope);
        case op_method:
            ++c.position;

            return parse_method(c, scope);
        case op_external:
            ++c.position;

            return parse_external(c, scope);
        case op_extended: {
            if (static_cast<usize>(c.end - c.position) < 2)
                return fail(aml_error::truncated);

            const u8 extended = c.position[1];

            c.position += 2;

            switch (extended) {
            case ext_mutex:
                return parse_mutex(c, scope);
            case ext_event:
                return parse_event(c, scope);
            case ext_operation_region:
                return parse_operation_region(c, scope);
            case ext_field:
                return parse_field(c, scope);
            case ext_device:
                return parse_scope_object(c, scope, node_kind::device);
            case ext_processor:
                return parse_scope_object(c, scope, node_kind::processor);
            case ext_power_resource:
                return parse_scope_object(c, scope, node_kind::power_resource);
            case ext_thermal_zone:
                return parse_scope_object(c, scope, node_kind::thermal_zone);
            case ext_index_field:
                return parse_index_field(c, scope);
            case ext_bank_field:
                return parse_bank_field(c, scope);
            case ext_data_table_region:
                return parse_data_table_region(c, scope);
            default:
                return fail(aml_error::unsupported_opcode);
            }
        }
        default:
            return fail(aml_error::unsupported_opcode);
        }
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

    bool interpreter::parse_scope_object(cursor& c, node_id scope, node_kind kind,
                                         u8 arg_count, u32 object_type) {
        const u8* package_end = nullptr;

        if (!open_package(c, package_end))
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

    bool interpreter::parse_method(cursor& c, node_id scope) {
        const u8* package_end = nullptr;

        if (!open_package(c, package_end))
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

    bool interpreter::parse_field_list(cursor& c, const u8* end, node_id scope) {
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

                if (*c.position == op_buffer) {
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

    bool interpreter::parse_field(cursor& c, node_id scope) {
        const u8* package_end = nullptr;

        if (!open_package(c, package_end))
            return false;

        name_ref region;

        if (!read_name_ref(c, region))
            return false;

        u8 flags = 0;

        if (!read_u8(c, flags))
            return false;

        return parse_field_list(c, package_end, scope) && close_package(c, package_end);
    }

    bool interpreter::parse_index_field(cursor& c, node_id scope) {
        const u8* package_end = nullptr;

        if (!open_package(c, package_end))
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

    bool interpreter::parse_bank_field(cursor& c, node_id scope) {
        const u8* package_end = nullptr;

        if (!open_package(c, package_end))
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

    bool interpreter::parse_data_table_region(cursor& c, node_id scope) {
        name_ref reference;

        if (!read_name_ref(c, reference))
            return false;

        for (u32 i = 0; i < 3; ++i) {
            if (c.position >= c.end)
                return fail(aml_error::truncated);

            if (*c.position++ != op_string_prefix)
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

    bool interpreter::parse_data_object(cursor& c, value_id& out) {
        term_guard depth{*this};

        if (!depth)
            return false;

        if (at_end(c))
            return fail(aml_error::truncated);

        if (*c.position == op_package)
            return parse_package(c, out, false);

        if (*c.position == op_var_package)
            return parse_package(c, out, true);

        if (*c.position == op_buffer)
            return parse_buffer(c, out);

        const u8 opcode = *c.position;

        switch (opcode) {
        case op_zero:
            ++c.position;
            out = add_integer(0);
            break;
        case op_one:
            ++c.position;
            out = add_integer(1);
            break;
        case op_ones:
            ++c.position;
            out = add_integer(~u64{0});
            break;
        case op_byte_prefix:
        case op_word_prefix:
        case op_dword_prefix:
        case op_qword_prefix: {
            ++c.position;

            u64 value = 0;

            if (opcode == op_byte_prefix) {
                u8 byte = 0;

                if (!read_u8(c, byte))
                    return false;

                value = byte;
            }
            else if (opcode == op_word_prefix) {
                u16 word = 0;

                if (!read_u16(c, word))
                    return false;

                value = word;
            }
            else if (opcode == op_dword_prefix) {
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
        case op_string_prefix: {
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
        case op_extended:
            if (static_cast<usize>(c.end - c.position) < 2)
                return fail(aml_error::truncated);

            if (c.position[1] == ext_revision) {
                c.position += 2;
                out = add_integer(interpreter_revision);
                break;
            }

            return fail(aml_error::unsupported_object);
        default: {
            if (opcode == op_root_char || opcode == op_parent_char ||
                opcode == op_dual_name || opcode == op_multi_name ||
                lead_name_char(opcode)) {
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

    bool interpreter::parse_package(cursor& c, value_id& out, bool variable) {
        if (at_end(c) || *c.position != (variable ? op_var_package : op_package))
            return fail(aml_error::truncated);

        ++c.position;

        const u8* package_end = nullptr;

        if (!open_package(c, package_end))
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

    bool interpreter::parse_buffer(cursor& c, value_id& out) {
        if (at_end(c) || *c.position != op_buffer)
            return fail(aml_error::truncated);

        ++c.position;

        const u8* package_end = nullptr;

        if (!open_package(c, package_end))
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

    bool interpreter::eval_constant(cursor& c, node_id scope, u64& out) {
        execution execution_context{};

        execution_context.scope = scope;

        value_id value = no_value;

        if (!eval_term(c, execution_context, value))
            return false;

        return as_integer(value, out);
    }

    bool interpreter::eval_term(cursor& c, execution& e, value_id& out) {
        term_guard depth{*this};

        if (!depth)
            return false;

        if (at_end(c))
            return fail(aml_error::truncated);

        const u8 opcode = *c.position;

        if (opcode >= op_local_base && opcode <= op_local_base + 7) {
            ++c.position;
            out = e.local[opcode - op_local_base];

            if (out == no_value)
                out = add_integer(0);

            return true;
        }

        if (opcode >= op_arg_base && opcode <= op_arg_base + 6) {
            ++c.position;
            out = e.argument[opcode - op_arg_base];

            if (out == no_value)
                out = add_integer(0);

            return true;
        }

        if (opcode == op_zero || opcode == op_one || opcode == op_ones ||
            opcode == op_byte_prefix || opcode == op_word_prefix ||
            opcode == op_dword_prefix || opcode == op_qword_prefix ||
            opcode == op_string_prefix)
            return parse_data_object(c, out);

        if (opcode == op_package || opcode == op_var_package)
            return eval_package(c, e, out, opcode == op_var_package);

        if (opcode == op_buffer)
            return eval_buffer(c, e, out);

        if (opcode == op_root_char || opcode == op_parent_char ||
            opcode == op_dual_name || opcode == op_multi_name || lead_name_char(opcode))
            return eval_name(c, e, out);

        switch (opcode) {
        case op_add:
        case op_subtract:
        case op_multiply:
        case op_and:
        case op_nand:
        case op_or:
        case op_nor:
        case op_xor:
        case op_shift_left:
        case op_shift_right:
        case op_mod: {
            ++c.position;

            value_id left  = no_value;
            value_id right = no_value;

            if (!eval_term(c, e, left) || !eval_term(c, e, right))
                return false;

            u64 a = 0;
            u64 b = 0;

            if (!as_integer(left, a) || !as_integer(right, b))
                return fail(aml_error::wrong_type);

            u64 result = 0;

            switch (opcode) {
            case op_add:
                result = a + b;
                break;
            case op_subtract:
                result = a - b;
                break;
            case op_multiply:
                result = a * b;
                break;
            case op_and:
                result = a & b;
                break;
            case op_nand:
                result = ~(a & b);
                break;
            case op_or:
                result = a | b;
                break;
            case op_nor:
                result = ~(a | b);
                break;
            case op_xor:
                result = a ^ b;
                break;
            case op_shift_left:
                result = b >= 64 ? 0 : a << b;
                break;
            case op_shift_right:
                result = b >= 64 ? 0 : a >> b;
                break;
            case op_mod:
                if (b == 0)
                    return fail(aml_error::invalid_value);

                result = a % b;
                break;
            default:
                break;
            }

            out = add_integer(result);

            return out != no_value || fail(aml_error::value_limit);
        }
        case op_divide: {
            ++c.position;

            value_id dividend = no_value;
            value_id divisor  = no_value;

            if (!eval_term(c, e, dividend) || !eval_term(c, e, divisor))
                return false;

            u64 a = 0;
            u64 b = 0;

            if (!as_integer(dividend, a) || !as_integer(divisor, b))
                return fail(aml_error::wrong_type);

            if (b == 0)
                return fail(aml_error::invalid_value);

            target remainder{};
            target quotient{};

            if (!eval_target(c, e, remainder) || !eval_target(c, e, quotient))
                return false;

            if (!remainder.discard) {
                const value_id value = add_integer(a % b);

                if (value == no_value)
                    return fail(aml_error::value_limit);

                *remainder.slot = value;
            }

            const value_id result = add_integer(a / b);

            if (result == no_value)
                return fail(aml_error::value_limit);

            if (!quotient.discard)
                *quotient.slot = result;

            out = result;

            return true;
        }
        case op_increment:
        case op_decrement: {
            ++c.position;

            target destination{};

            if (!eval_target(c, e, destination))
                return false;

            u64 value = 0;

            if (!destination.discard) {
                if (!as_integer(*destination.slot, value))
                    return fail(aml_error::wrong_type);

                value = opcode == op_increment ? value + 1 : value - 1;

                const value_id result = add_integer(value);

                if (result == no_value)
                    return fail(aml_error::value_limit);

                *destination.slot = result;
                out               = result;

                return true;
            }

            out = add_integer(0);

            return out != no_value || fail(aml_error::value_limit);
        }
        case op_not: {
            ++c.position;

            value_id operand = no_value;

            if (!eval_term(c, e, operand))
                return false;

            u64 value = 0;

            if (!as_integer(operand, value))
                return fail(aml_error::wrong_type);

            out = add_integer(~value);

            return out != no_value || fail(aml_error::value_limit);
        }
        case op_find_left:
        case op_find_right: {
            ++c.position;

            value_id operand = no_value;

            if (!eval_term(c, e, operand))
                return false;

            u64 value = 0;

            if (!as_integer(operand, value))
                return fail(aml_error::wrong_type);

            u64 result = 0;

            if (opcode == op_find_left) {
                for (u64 bit = 63; bit != ~u64{0}; --bit) {
                    if ((value & (1ull << bit)) != 0) {
                        result = bit + 1;

                        break;
                    }
                }
            }
            else {
                for (u64 bit = 0; bit < 64; ++bit) {
                    if ((value & (1ull << bit)) != 0) {
                        result = bit + 1;

                        break;
                    }
                }
            }

            out = add_integer(result);

            return out != no_value || fail(aml_error::value_limit);
        }
        case op_deref: {
            ++c.position;

            value_id operand = no_value;

            if (!eval_term(c, e, operand))
                return false;

            out = dereference(operand);

            if (out == no_value)
                return fail(aml_error::missing_object);

            return true;
        }
        case op_size_of: {
            ++c.position;

            value_id operand = no_value;

            if (!eval_term(c, e, operand))
                return false;

            const value_id resolved = dereference(operand);

            if (resolved == no_value)
                return fail(aml_error::missing_object);

            const value& entry = _values[resolved];

            u64 size = 0;

            if (entry.kind == static_cast<u8>(value_kind::package))
                size = entry.count;
            else if (entry.kind == static_cast<u8>(value_kind::string) ||
                     entry.kind == static_cast<u8>(value_kind::buffer))
                size = entry.length;
            else if (entry.kind == static_cast<u8>(value_kind::integer))
                size = 8;
            else
                return fail(aml_error::wrong_type);

            out = add_integer(size);

            return out != no_value || fail(aml_error::value_limit);
        }
        case op_index: {
            ++c.position;

            value_id container = no_value;
            value_id index     = no_value;

            if (!eval_term(c, e, container) || !eval_term(c, e, index))
                return false;

            u64 position = 0;

            if (!as_integer(index, position))
                return fail(aml_error::wrong_type);

            const value_id resolved = dereference(container);

            if (resolved == no_value ||
                _values[resolved].kind != static_cast<u8>(value_kind::package))
                return fail(aml_error::wrong_type);

            if (position >= _values[resolved].count)
                return fail(aml_error::invalid_value);

            value_id item = _values[resolved].first;

            for (u64 i = 0; i < position; ++i) {
                item = _values[item].next;
            }

            out = item;

            return out != no_value || fail(aml_error::missing_object);
        }
        case op_object_type: {
            ++c.position;

            value_id operand = no_value;

            if (!eval_term(c, e, operand))
                return false;

            const value_id resolved = dereference(operand);

            if (resolved == no_value)
                return fail(aml_error::missing_object);

            out = add_integer(_values[resolved].kind);

            return out != no_value || fail(aml_error::value_limit);
        }
        case op_l_and:
        case op_l_or: {
            ++c.position;

            value_id left  = no_value;
            value_id right = no_value;

            if (!eval_term(c, e, left) || !eval_term(c, e, right))
                return false;

            u64 a = 0;
            u64 b = 0;

            if (!as_integer(left, a) || !as_integer(right, b))
                return fail(aml_error::wrong_type);

            const bool result = opcode == op_l_and ? a != 0 && b != 0 : a != 0 || b != 0;

            out = add_integer(result ? 1 : 0);

            return out != no_value || fail(aml_error::value_limit);
        }
        case op_l_not: {
            ++c.position;

            value_id operand = no_value;

            if (!eval_term(c, e, operand))
                return false;

            u64 value = 0;

            if (!as_integer(operand, value))
                return fail(aml_error::wrong_type);

            out = add_integer(value == 0 ? 1 : 0);

            return out != no_value || fail(aml_error::value_limit);
        }
        case op_l_equal:
        case op_l_greater:
        case op_l_less: {
            ++c.position;

            value_id left  = no_value;
            value_id right = no_value;

            if (!eval_term(c, e, left) || !eval_term(c, e, right))
                return false;

            u64 a = 0;
            u64 b = 0;

            if (!as_integer(left, a) || !as_integer(right, b))
                return fail(aml_error::wrong_type);

            bool result = false;

            if (opcode == op_l_equal)
                result = a == b;
            else if (opcode == op_l_greater)
                result = a > b;
            else
                result = a < b;

            out = add_integer(result ? 1 : 0);

            return out != no_value || fail(aml_error::value_limit);
        }
        case op_concatenate: {
            ++c.position;

            value_id left  = no_value;
            value_id right = no_value;

            if (!eval_term(c, e, left) || !eval_term(c, e, right))
                return false;

            u64 a = 0;
            u64 b = 0;

            if (as_integer(left, a) && as_integer(right, b)) {
                out = add_integer(a | (b << 32));

                return out != no_value || fail(aml_error::value_limit);
            }

            return fail(aml_error::unsupported_object);
        }
        case op_to_integer: {
            ++c.position;

            value_id operand = no_value;

            if (!eval_term(c, e, operand))
                return false;

            out = dereference(operand);

            if (out == no_value)
                return fail(aml_error::missing_object);

            if (_values[out].kind == static_cast<u8>(value_kind::integer))
                return true;

            return fail(aml_error::wrong_type);
        }
        case op_copy_object: {
            ++c.position;

            value_id source = no_value;

            if (!eval_term(c, e, source))
                return false;

            target destination{};

            if (!eval_target(c, e, destination))
                return false;

            if (!destination.discard)
                *destination.slot = source;

            out = source;

            return true;
        }
        case op_store: {
            ++c.position;

            value_id source = no_value;

            if (!eval_term(c, e, source))
                return false;

            target destination{};

            if (!eval_target(c, e, destination))
                return false;

            if (!destination.discard)
                *destination.slot = source;

            out = source;

            return true;
        }
        case op_return: {
            ++c.position;

            value_id result = no_value;

            if (!eval_term(c, e, result))
                return false;

            e.returned   = result;
            e.has_return = true;
            out          = result;

            return true;
        }
        case op_if: {
            if (!execute_if(c, e))
                return false;

            out = add_integer(0);

            return out != no_value || fail(aml_error::value_limit);
        }
        case op_while: {
            if (!execute_while(c, e))
                return false;

            out = add_integer(0);

            return out != no_value || fail(aml_error::value_limit);
        }
        case op_notify: {
            ++c.position;

            value_id ignored = no_value;

            if (!eval_term(c, e, ignored) || !eval_term(c, e, ignored))
                return false;

            out = add_integer(0);

            return out != no_value || fail(aml_error::value_limit);
        }
        case op_noop: {
            ++c.position;
            out = add_integer(0);

            return out != no_value || fail(aml_error::value_limit);
        }
        case op_break: {
            ++c.position;
            e.do_break = true;
            out        = add_integer(0);

            return out != no_value || fail(aml_error::value_limit);
        }
        case op_continue: {
            ++c.position;
            e.do_continue = true;
            out           = add_integer(0);

            return out != no_value || fail(aml_error::value_limit);
        }
        case op_extended: {
            if (static_cast<usize>(c.end - c.position) < 2)
                return fail(aml_error::truncated);

            const u8 extended = c.position[1];

            c.position += 2;

            if (extended == ext_revision) {
                out = add_integer(interpreter_revision);

                return out != no_value || fail(aml_error::value_limit);
            }

            if (extended == ext_debug) {
                out = add_integer(0);

                return out != no_value || fail(aml_error::value_limit);
            }

            if (extended == ext_fatal) {
                u8  type = 0;
                u32 code = 0;

                if (!read_u8(c, type) || !read_u32(c, code))
                    return false;

                value_id ignored = no_value;

                if (!eval_term(c, e, ignored))
                    return false;

                out = add_integer(0);

                return out != no_value || fail(aml_error::value_limit);
            }

            if (extended == ext_mutex || extended == ext_event) {
                out = add_integer(0);

                return out != no_value || fail(aml_error::value_limit);
            }

            return fail(aml_error::unsupported_opcode);
        }
        default:
            return fail(aml_error::unsupported_opcode);
        }
    }

    bool interpreter::eval_name(cursor& c, execution& e, value_id& out) {
        name_ref reference;

        if (!read_name_ref(c, reference))
            return false;

        const node_id resolved = resolve_lookup(reference, e.scope);

        if (resolved == no_node)
            return fail(aml_error::missing_object);

        const node_id target_id = follow_alias(resolved, 0);

        if (target_id == no_node)
            return fail(aml_error::missing_object);

        const node& entry = _nodes[target_id];

        if (entry.kind == static_cast<u8>(node_kind::method)) {
            value_id arguments[7]{};

            for (u8 i = 0; i < entry.arg_count; ++i) {
                if (!eval_term(c, e, arguments[i]))
                    return false;
            }

            return execute_method(target_id, arguments, entry.arg_count, out);
        }

        if (entry.value == no_value)
            return fail(aml_error::missing_object);

        out = entry.value;

        return true;
    }

    bool interpreter::eval_package(cursor& c, execution& e, value_id& out,
                                   bool variable) {
        if (at_end(c) || *c.position != (variable ? op_var_package : op_package))
            return fail(aml_error::truncated);

        ++c.position;

        const u8* package_end = nullptr;

        if (!open_package(c, package_end))
            return false;

        u32 count = 0;

        if (variable) {
            value_id count_value = no_value;

            if (!eval_term(c, e, count_value))
                return false;

            u64 value = 0;

            if (!as_integer(count_value, value))
                return fail(aml_error::wrong_type);

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

            if (!eval_term(c, e, item))
                return false;

            if (!append_package_item(package, item))
                return false;
        }

        if (c.position != package_end)
            return fail(aml_error::package_length);

        out = package;

        return close_package(c, package_end);
    }

    bool interpreter::eval_buffer(cursor& c, execution& e, value_id& out) {
        if (at_end(c) || *c.position != op_buffer)
            return fail(aml_error::truncated);

        ++c.position;

        const u8* package_end = nullptr;

        if (!open_package(c, package_end))
            return false;

        value_id size_value = no_value;

        if (!eval_term(c, e, size_value))
            return false;

        u64 size = 0;

        if (!as_integer(size_value, size))
            return fail(aml_error::wrong_type);

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

    bool interpreter::eval_target(cursor& c, execution& e, target& out) {
        if (at_end(c))
            return fail(aml_error::truncated);

        const u8 opcode = *c.position;

        if (opcode >= op_local_base && opcode <= op_local_base + 7) {
            ++c.position;
            out.slot = &e.local[opcode - op_local_base];

            return true;
        }

        if (opcode >= op_arg_base && opcode <= op_arg_base + 6) {
            ++c.position;
            out.slot = &e.argument[opcode - op_arg_base];

            return true;
        }

        if (opcode == op_root_char || opcode == op_parent_char ||
            opcode == op_dual_name || opcode == op_multi_name || lead_name_char(opcode)) {
            name_ref reference;

            if (!read_name_ref(c, reference))
                return false;

            const node_id resolved = resolve_lookup(reference, e.scope);

            if (resolved == no_node)
                return fail(aml_error::missing_object);

            const node_id target_id = follow_alias(resolved, 0);

            if (target_id == no_node)
                return fail(aml_error::missing_object);

            return eval_target_for_node(target_id, out);
        }

        if (opcode == op_extended && static_cast<usize>(c.end - c.position) >= 2 &&
            c.position[1] == ext_debug) {
            c.position += 2;
            out.discard = true;

            return true;
        }

        return fail(aml_error::unsupported_object);
    }

    bool interpreter::eval_target_for_node(node_id id, target& out) {
        if (id >= _node_count)
            return fail(aml_error::missing_object);

        node& entry = _nodes[id];

        if (entry.kind != static_cast<u8>(node_kind::name) &&
            entry.kind != static_cast<u8>(node_kind::field))
            return fail(aml_error::wrong_type);

        out.slot = &entry.value;

        return true;
    }

    bool interpreter::execute_statement(cursor& c, execution& e) {
        term_guard depth{*this};

        if (!depth)
            return false;

        if (at_end(c))
            return false;

        const u8 opcode = *c.position;

        switch (opcode) {
        case op_if:
            return execute_if(c, e);
        case op_while:
            return execute_while(c, e);
        case op_return: {
            ++c.position;

            value_id result = no_value;

            if (!eval_term(c, e, result))
                return false;

            e.returned   = result;
            e.has_return = true;

            return true;
        }
        case op_break:
            ++c.position;
            e.do_break = true;

            return true;
        case op_continue:
            ++c.position;
            e.do_continue = true;

            return true;
        case op_noop:
            ++c.position;
            return true;
        case op_store: {
            value_id ignored = no_value;

            return eval_term(c, e, ignored);
        }
        case op_extended: {
            if (static_cast<usize>(c.end - c.position) < 2)
                return fail(aml_error::truncated);

            const u8 extended = c.position[1];

            if (extended == ext_revision || extended == ext_debug) {
                value_id ignored = no_value;

                return eval_term(c, e, ignored);
            }

            return fail(aml_error::unsupported_opcode);
        }
        default: {
            value_id ignored = no_value;

            return eval_term(c, e, ignored);
        }
        }
    }

    bool interpreter::execute_if(cursor& c, execution& e) {
        const u8* package_end = nullptr;

        if (c.position == nullptr || c.position >= c.end || *c.position != op_if)
            return fail(aml_error::truncated);

        ++c.position;

        if (!open_package(c, package_end))
            return false;

        value_id predicate = no_value;

        if (!eval_term(c, e, predicate))
            return false;

        u64 condition = 0;

        if (!as_integer(predicate, condition))
            return fail(aml_error::wrong_type);

        if (condition != 0) {
            while (c.position < package_end && !e.has_return && !e.do_break &&
                   !e.do_continue) {
                if (!step() || !execute_statement(c, e))
                    return false;
            }
        }

        c.position = package_end;

        if (!close_package(c, package_end))
            return false;

        if (c.position < c.end && *c.position == op_else) {
            ++c.position;

            const u8* else_end = nullptr;

            if (!open_package(c, else_end))
                return false;

            if (condition == 0) {
                while (c.position < else_end && !e.has_return && !e.do_break &&
                       !e.do_continue) {
                    if (!step() || !execute_statement(c, e))
                        return false;
                }
            }

            c.position = else_end;

            if (!close_package(c, else_end))
                return false;
        }

        return true;
    }

    bool interpreter::execute_while(cursor& c, execution& e) {
        const u8* package_end = nullptr;

        if (c.position == nullptr || c.position >= c.end || *c.position != op_while)
            return fail(aml_error::truncated);

        ++c.position;

        if (!open_package(c, package_end))
            return false;

        const u8* const body_start = c.position;

        while (true) {
            if (!step())
                return false;

            cursor probe{
                body_start,
                package_end,
            };

            value_id predicate = no_value;

            if (!eval_term(probe, e, predicate))
                return false;

            u64 condition = 0;

            if (!as_integer(predicate, condition))
                return fail(aml_error::wrong_type);

            if (condition == 0)
                break;

            c.position = probe.position;

            while (c.position < package_end && !e.has_return && !e.do_break &&
                   !e.do_continue) {
                if (!step() || !execute_statement(c, e))
                    return false;
            }

            if (e.has_return || e.do_break) {
                e.do_break = false;

                break;
            }

            e.do_continue = false;
        }

        c.position = package_end;

        return close_package(c, package_end);
    }

    bool interpreter::execute_method(node_id method, const value_id* arguments,
                                     usize count, value_id& out) {
        if (method >= _node_count ||
            _nodes[method].kind != static_cast<u8>(node_kind::method))
            return fail(aml_error::wrong_type);

        if (count != _nodes[method].arg_count)
            return fail(aml_error::method_arguments);

        if (_call_depth == max_aml_call_depth)
            return fail(aml_error::recursion_limit);

        execution e{};

        e.scope = _nodes[method].parent;

        for (value_id& local : e.local) {
            local = add_integer(0);

            if (local == no_value)
                return fail(aml_error::value_limit);
        }

        for (usize i = 0; i < count && i < 7; ++i) {
            e.argument[i] = arguments[i];
        }

        const node& entry = _nodes[method];

        cursor c{
            entry.body,
            entry.body + entry.body_length,
        };

        ++_call_depth;

        bool ok = true;

        while (ok && !at_end(c) && !e.has_return) {
            ok = step() && execute_statement(c, e);
        }

        --_call_depth;

        if (!ok)
            return false;

        if (e.has_return)
            out = e.returned;
        else {
            out = add_integer(0);

            if (out == no_value)
                return fail(aml_error::value_limit);
        }

        return true;
    }

    bool interpreter::step() {
        if (_steps == max_aml_eval_steps)
            return fail(aml_error::execution_limit);

        ++_steps;

        return true;
    }

    bool interpreter::evaluate_s5(value_id& package) {
        const node_id s5_node =
            find_child(root(), name_segment('_', 'S', '5', '_'));

        if (s5_node == no_node)
            return fail(aml_error::missing_object);

        const node_id s5 = follow_alias(s5_node, 0);

        if (s5 == no_node)
            return fail(aml_error::missing_object);

        const node& entry = _nodes[s5];

        if (entry.kind == static_cast<u8>(node_kind::method)) {
            if (entry.arg_count != 0)
                return fail(aml_error::method_arguments);

            value_id result = no_value;

            if (!execute_method(s5, nullptr, 0, result))
                return false;

            package = dereference(result);
        }
        else if (entry.kind == static_cast<u8>(node_kind::name)) {
            package = dereference(entry.value);
        }
        else {
            return fail(aml_error::wrong_type);
        }

        if (package == no_value ||
            _values[package].kind != static_cast<u8>(value_kind::package))
            return fail(aml_error::wrong_type);

        return true;
    }
} // namespace astia::acpi::detail
