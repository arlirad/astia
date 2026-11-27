#include "aml_internal.hpp"

namespace astia::acpi::detail {
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

    bool interpreter::step() {
        if (_steps == max_aml_eval_steps)
            return fail(aml_error::execution_limit);

        ++_steps;

        return true;
    }

    bool interpreter::evaluate_s5(value_id& package) {
        const node_id s5_node = find_child(root(), name_segment('_', 'S', '5', '_'));
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
