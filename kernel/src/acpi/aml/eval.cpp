#include "aml_internal.hpp"

namespace astia::acpi::detail {
    bool interpreter::eval_buffer(cursor& c, execution& e, value_id& out) {
        if (at_end(c) || *c.position != raw(opcode::buffer))
            return fail(aml_error::truncated);

        ++c.position;

        const u8* package_end = open_package(c);
        if (package_end == nullptr)
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

    bool interpreter::eval_create_field(cursor& c, execution& e, value_id& out, u32 width,
                                        bool bit_index) {
        value_id source = no_value;
        value_id index  = no_value;

        if (!eval_term(c, e, source) || !eval_term(c, e, index))
            return false;

        u64 position = 0;

        if (!as_integer(index, position))
            return fail(aml_error::wrong_type);

        name_ref reference;

        if (!read_name_ref(c, reference))
            return false;

        const node_id target = resolve_definition(reference, e.scope, true);
        if (target == no_node)
            return fail(aml_error::object_limit);

        const value_id buffer = dereference(source);

        if (buffer == no_value ||
            _values[buffer].kind != static_cast<u8>(value_kind::buffer))
            return fail(aml_error::wrong_type);

        if (!bit_index && position > ~u64{0} / 8)
            return fail(aml_error::invalid_value);

        const u64 bit_offset  = position * (bit_index ? 1ull : 8ull);
        const u64 buffer_bits = static_cast<u64>(_values[buffer].length) * 8;

        if (width > 64 || bit_offset > buffer_bits || width > buffer_bits - bit_offset ||
            bit_offset > ~u32{0})
            return fail(aml_error::invalid_value);

        node& entry = _nodes[target];

        entry.kind       = static_cast<u8>(node_kind::field);
        entry.source     = buffer;
        entry.region     = no_node;
        entry.bit_offset = static_cast<u32>(bit_offset);
        entry.bit_width  = width;
        entry.value      = no_value;

        out = add_integer(0);

        return out != no_value || fail(aml_error::value_limit);
    }

    bool interpreter::eval_constant(cursor& c, node_id scope, u64& out) {
        execution execution_context{};

        execution_context.scope = scope;

        value_id value = no_value;

        if (!eval_term(c, execution_context, value))
            return false;

        return as_integer(value, out);
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

        if (entry.kind == static_cast<u8>(node_kind::field)) {
            u64 value = 0;

            if (!read_field(target_id, value))
                return false;

            out = add_integer(value);

            return out != no_value || fail(aml_error::value_limit);
        }

        if (entry.value == no_value)
            return fail(aml_error::missing_object);

        out = entry.value;

        return true;
    }

    bool interpreter::eval_package(cursor& c, execution& e, value_id& out,
                                   bool variable) {
        if (at_end(c) ||
            *c.position != (variable ? raw(opcode::var_package) : raw(opcode::package)))
            return fail(aml_error::truncated);

        ++c.position;

        const u8* package_end = open_package(c);
        if (package_end == nullptr)
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

    bool interpreter::eval_target(cursor& c, execution& e, target& out) {
        if (at_end(c))
            return fail(aml_error::truncated);

        const auto op = static_cast<opcode>(*c.position);

        if (op == opcode::zero) {
            ++c.position;

            out.slot  = nullptr;
            out.field = no_node;

            return true;
        }

        if (raw(op) >= raw(opcode::local_base) &&
            raw(op) <= raw(opcode::local_base) + 7) {
            ++c.position;

            out.slot = &e.local[raw(op) - raw(opcode::local_base)];

            return true;
        }

        if (raw(op) >= raw(opcode::arg_base) && raw(op) <= raw(opcode::arg_base) + 6) {
            ++c.position;

            out.slot = &e.argument[raw(op) - raw(opcode::arg_base)];

            return true;
        }

        if (op == opcode::root_char || op == opcode::parent_char ||
            op == opcode::dual_name || op == opcode::multi_name ||
            lead_name_char(raw(op))) {
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

        if (op == opcode::extended && static_cast<usz>(c.end - c.position) >= 2 &&
            c.position[1] == raw(extended_opcode::debug)) {
            c.position += 2;
            out.slot = nullptr;

            return true;
        }

        return fail(aml_error::unsupported_object);
    }

    bool interpreter::eval_target_for_node(node_id id, target& out) {
        if (id >= _node_count)
            return fail(aml_error::missing_object);

        node& entry = _nodes[id];

        if (entry.kind == static_cast<u8>(node_kind::name)) {
            out.slot      = &entry.value;
            out.name      = id;
            out.transient = entry.transient;

            return true;
        }

        if (entry.kind == static_cast<u8>(node_kind::field)) {
            out.field = id;

            return true;
        }

        return fail(aml_error::wrong_type);
    }

    bool interpreter::finish_term(cursor& c, execution& e, value_id result,
                                  value_id& out) {
        target destination{};

        if (!eval_target(c, e, destination))
            return false;

        if (!write_target(destination, result))
            return false;

        out = result;

        return true;
    }

    bool interpreter::eval_term(cursor& c, execution& e, value_id& out) {
        term_guard depth{*this};

        if (!depth)
            return false;

        if (at_end(c))
            return fail(aml_error::truncated);

        const auto op = static_cast<opcode>(*c.position);

        if (raw(op) >= raw(opcode::local_base) &&
            raw(op) <= raw(opcode::local_base) + 7) {
            ++c.position;

            out = e.local[raw(op) - raw(opcode::local_base)];
            if (out == no_value)
                out = add_integer(0);

            return true;
        }

        if (raw(op) >= raw(opcode::arg_base) && raw(op) <= raw(opcode::arg_base) + 6) {
            ++c.position;

            out = e.argument[raw(op) - raw(opcode::arg_base)];
            if (out == no_value)
                out = add_integer(0);

            return true;
        }

        if (op == opcode::zero || op == opcode::one || op == opcode::ones ||
            op == opcode::byte_prefix || op == opcode::word_prefix ||
            op == opcode::dword_prefix || op == opcode::qword_prefix ||
            op == opcode::string_prefix)
            return parse_data_object(c, out);

        if (op == opcode::package || op == opcode::var_package)
            return eval_package(c, e, out, op == opcode::var_package);

        if (op == opcode::buffer)
            return eval_buffer(c, e, out);

        if (op == opcode::root_char || op == opcode::parent_char ||
            op == opcode::dual_name || op == opcode::multi_name ||
            lead_name_char(raw(op)))
            return eval_name(c, e, out);

        switch (op) {
        case opcode::add:
        case opcode::subtract:
        case opcode::multiply:
        case opcode::bit_and:
        case opcode::nand:
        case opcode::bit_or:
        case opcode::nor:
        case opcode::bit_xor:
        case opcode::shift_left:
        case opcode::shift_right:
        case opcode::mod: {
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

            switch (op) {
            case opcode::add:
                result = a + b;

                break;
            case opcode::subtract:
                result = a - b;

                break;
            case opcode::multiply:
                result = a * b;

                break;
            case opcode::bit_and:
                result = a & b;

                break;
            case opcode::nand:
                result = ~(a & b);

                break;
            case opcode::bit_or:
                result = a | b;

                break;
            case opcode::nor:
                result = ~(a | b);

                break;
            case opcode::bit_xor:
                result = a ^ b;

                break;
            case opcode::shift_left:
                result = b >= 64 ? 0 : a << b;

                break;
            case opcode::shift_right:
                result = b >= 64 ? 0 : a >> b;

                break;
            case opcode::mod:
                if (b == 0)
                    return fail(aml_error::invalid_value);

                result = a % b;

                break;
            default:
                break;
            }

            const value_id computed = add_integer(result);
            if (computed == no_value)
                return fail(aml_error::value_limit);

            return finish_term(c, e, computed, out);
        }
        case opcode::divide: {
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

            const value_id remainder_value = add_integer(a % b);
            if (remainder_value == no_value)
                return fail(aml_error::value_limit);

            if (remainder.slot != nullptr || remainder.field != no_node) {
                if (!write_target(remainder, remainder_value))
                    return false;
            }

            const value_id result = add_integer(a / b);
            if (result == no_value)
                return fail(aml_error::value_limit);

            if (quotient.slot != nullptr || quotient.field != no_node) {
                if (!write_target(quotient, result))
                    return false;
            }

            out = result;

            return true;
        }
        case opcode::increment:
        case opcode::decrement: {
            ++c.position;

            target destination{};

            if (!eval_target(c, e, destination))
                return false;

            u64 value = 0;

            if (!read_target(destination, value))
                return false;

            value = op == opcode::increment ? value + 1 : value - 1;

            const value_id result = add_integer(value);

            if (result == no_value)
                return fail(aml_error::value_limit);

            if (!write_target(destination, result))
                return false;

            out = result;

            return true;
        }
        case opcode::bit_not: {
            ++c.position;

            value_id operand = no_value;

            if (!eval_term(c, e, operand))
                return false;

            u64 value = 0;

            if (!as_integer(operand, value))
                return fail(aml_error::wrong_type);

            const value_id computed = add_integer(~value);
            if (computed == no_value)
                return fail(aml_error::value_limit);

            return finish_term(c, e, computed, out);
        }
        case opcode::find_left:
        case opcode::find_right: {
            ++c.position;

            value_id operand = no_value;

            if (!eval_term(c, e, operand))
                return false;

            u64 value = 0;

            if (!as_integer(operand, value))
                return fail(aml_error::wrong_type);

            u64 result = 0;

            if (op == opcode::find_left) {
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

            const value_id computed = add_integer(result);
            if (computed == no_value)
                return fail(aml_error::value_limit);

            return finish_term(c, e, computed, out);
        }
        case opcode::deref: {
            ++c.position;

            value_id operand = no_value;

            if (!eval_term(c, e, operand))
                return false;

            out = dereference(operand);
            if (out == no_value)
                return fail(aml_error::missing_object);

            return true;
        }
        case opcode::size_of: {
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
        case opcode::index: {
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
        case opcode::create_dword_field:
        case opcode::create_word_field:
        case opcode::create_byte_field:
        case opcode::create_bit_field:
        case opcode::create_qword_field: {
            ++c.position;

            u32  width     = 0;
            bool bit_index = false;

            switch (op) {
            case opcode::create_dword_field:
                width = 32;

                break;
            case opcode::create_word_field:
                width = 16;

                break;
            case opcode::create_byte_field:
                width = 8;

                break;
            case opcode::create_bit_field:
                width     = 1;
                bit_index = true;

                break;
            case opcode::create_qword_field:
                width = 64;

                break;
            default:
                break;
            }

            return eval_create_field(c, e, out, width, bit_index);
        }
        case opcode::object_type: {
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
        case opcode::l_and:
        case opcode::l_or: {
            ++c.position;

            value_id left  = no_value;
            value_id right = no_value;

            if (!eval_term(c, e, left) || !eval_term(c, e, right))
                return false;

            u64 a = 0;
            u64 b = 0;

            if (!as_integer(left, a) || !as_integer(right, b))
                return fail(aml_error::wrong_type);

            const bool result = op == opcode::l_and ? a != 0 && b != 0 : a != 0 || b != 0;

            out = add_integer(result ? 1 : 0);

            return out != no_value || fail(aml_error::value_limit);
        }
        case opcode::l_not: {
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
        case opcode::l_equal:
        case opcode::l_greater:
        case opcode::l_less: {
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

            if (op == opcode::l_equal)
                result = a == b;
            else if (op == opcode::l_greater)
                result = a > b;
            else
                result = a < b;

            out = add_integer(result ? 1 : 0);

            return out != no_value || fail(aml_error::value_limit);
        }
        case opcode::concatenate: {
            ++c.position;

            value_id left  = no_value;
            value_id right = no_value;

            if (!eval_term(c, e, left) || !eval_term(c, e, right))
                return false;

            u64 a = 0;
            u64 b = 0;

            if (as_integer(left, a) && as_integer(right, b)) {
                const value_id computed = add_integer(a | (b << 32));
                if (computed == no_value)
                    return fail(aml_error::value_limit);

                return finish_term(c, e, computed, out);
            }

            return fail(aml_error::unsupported_object);
        }
        case opcode::to_integer: {
            ++c.position;

            value_id operand = no_value;

            if (!eval_term(c, e, operand))
                return false;

            const value_id resolved = dereference(operand);
            if (resolved == no_value)
                return fail(aml_error::missing_object);

            if (_values[resolved].kind != static_cast<u8>(value_kind::integer))
                return fail(aml_error::wrong_type);

            return finish_term(c, e, resolved, out);
        }
        case opcode::copy_object: {
            ++c.position;

            value_id source = no_value;

            if (!eval_term(c, e, source))
                return false;

            target destination{};

            if (!eval_target(c, e, destination))
                return false;

            if (!write_target(destination, source))
                return false;

            out = source;

            return true;
        }
        case opcode::store: {
            ++c.position;

            value_id source = no_value;

            if (!eval_term(c, e, source))
                return false;

            target destination{};

            if (!eval_target(c, e, destination))
                return false;

            if (!write_target(destination, source))
                return false;

            out = source;

            return true;
        }
        case opcode::return_statement: {
            ++c.position;

            value_id result = no_value;

            if (!eval_term(c, e, result))
                return false;

            e.returned   = result;
            e.has_return = true;
            out          = result;

            return true;
        }
        case opcode::if_statement: {
            if (!execute_if(c, e))
                return false;

            out = add_integer(0);

            return out != no_value || fail(aml_error::value_limit);
        }
        case opcode::while_statement: {
            if (!execute_while(c, e))
                return false;

            out = add_integer(0);

            return out != no_value || fail(aml_error::value_limit);
        }
        case opcode::notify: {
            ++c.position;

            value_id ignored = no_value;

            if (!eval_term(c, e, ignored) || !eval_term(c, e, ignored))
                return false;

            out = add_integer(0);

            return out != no_value || fail(aml_error::value_limit);
        }
        case opcode::noop: {
            ++c.position;

            out = add_integer(0);

            return out != no_value || fail(aml_error::value_limit);
        }
        case opcode::break_statement: {
            ++c.position;

            e.do_break = true;
            out        = add_integer(0);

            return out != no_value || fail(aml_error::value_limit);
        }
        case opcode::continue_statement: {
            ++c.position;

            e.do_continue = true;
            out           = add_integer(0);

            return out != no_value || fail(aml_error::value_limit);
        }
        case opcode::extended: {
            if (static_cast<usz>(c.end - c.position) < 2)
                return fail(aml_error::truncated);

            const auto extended = static_cast<extended_opcode>(c.position[1]);

            c.position += 2;

            if (extended == extended_opcode::revision) {
                out = add_integer(interpreter_revision);

                return out != no_value || fail(aml_error::value_limit);
            }

            if (extended == extended_opcode::debug) {
                out = add_integer(0);

                return out != no_value || fail(aml_error::value_limit);
            }

            if (extended == extended_opcode::fatal) {
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

            if (extended == extended_opcode::mutex ||
                extended == extended_opcode::event) {
                out = add_integer(0);

                return out != no_value || fail(aml_error::value_limit);
            }

            return fail(aml_error::unsupported_opcode);
        }
        default:
            return fail(aml_error::unsupported_opcode);
        }
    }

    bool interpreter::execute_if(cursor& c, execution& e) {
        if (c.position == nullptr || c.position >= c.end ||
            *c.position != raw(opcode::if_statement))
            return fail(aml_error::truncated);

        ++c.position;

        const u8* package_end = open_package(c);
        if (package_end == nullptr)
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

        if (c.position < c.end && *c.position == raw(opcode::else_statement)) {
            ++c.position;

            const u8* else_end = open_package(c);
            if (else_end == nullptr)
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

    bool interpreter::execute_method(node_id method, const value_id* arguments, usz count,
                                     value_id& out) {
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

        for (usz i = 0; i < count && i < 7; ++i) {
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

        if (e.has_return) {
            out = e.returned;
        }
        else {
            out = add_integer(0);
            if (out == no_value)
                return fail(aml_error::value_limit);
        }

        return true;
    }

    bool interpreter::execute_statement(cursor& c, execution& e) {
        term_guard depth{*this};

        if (!depth)
            return false;

        if (at_end(c))
            return false;

        const auto op = static_cast<opcode>(*c.position);

        switch (op) {
        case opcode::name:
            ++c.position;

            return parse_name_object(c, e.scope);
        case opcode::if_statement:
            return execute_if(c, e);
        case opcode::while_statement:
            return execute_while(c, e);
        case opcode::return_statement: {
            ++c.position;

            value_id result = no_value;

            if (!eval_term(c, e, result))
                return false;

            e.returned   = result;
            e.has_return = true;

            return true;
        }
        case opcode::break_statement:
            ++c.position;

            e.do_break = true;

            return true;
        case opcode::continue_statement:
            ++c.position;

            e.do_continue = true;

            return true;
        case opcode::noop:
            ++c.position;

            return true;
        case opcode::store: {
            value_id ignored = no_value;

            return eval_term(c, e, ignored);
        }
        case opcode::extended: {
            if (static_cast<usz>(c.end - c.position) < 2)
                return fail(aml_error::truncated);

            const auto extended = static_cast<extended_opcode>(c.position[1]);

            if (extended == extended_opcode::revision ||
                extended == extended_opcode::debug) {
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

    bool interpreter::execute_while(cursor& c, execution& e) {
        if (c.position == nullptr || c.position >= c.end ||
            *c.position != raw(opcode::while_statement))
            return fail(aml_error::truncated);

        ++c.position;

        const u8* package_end = open_package(c);
        if (package_end == nullptr)
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
} // namespace astia::acpi::detail
