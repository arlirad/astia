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
        _node_count             = 0;
        _value_count            = 0;
        _reference_count        = 0;
        _buffer_used            = 0;
        _persistent_buffer_used = 0;
        _error                  = aml_error::none;
        _steps                  = 0;
        _call_depth             = 0;
        _term_depth             = 0;
        _package_depth          = 0;

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

        _static_values     = _value_count;
        _static_references = _reference_count;

        return _error == aml_error::none;
    }

    void interpreter::reset_evaluation() {
        bool dynamic_value = false;

        for (node_id id = 0; id < _node_count; ++id) {
            const node& entry = _nodes[id];

            if (entry.transient)
                continue;

            if (entry.value != no_value && entry.value >= _static_values)
                dynamic_value = true;

            if (entry.source != no_value && entry.source >= _static_values)
                dynamic_value = true;
        }

        if (!dynamic_value) {
            _value_count     = _static_values;
            _reference_count = _static_references;
            _buffer_used     = 0;
        }

        _error         = aml_error::none;
        _steps         = 0;
        _call_depth    = 0;
        _term_depth    = 0;
        _package_depth = 0;
    }

    bool interpreter::parse_path(const char* path, name_ref& out) const {
        out = name_ref{};

        if (path == nullptr)
            return false;

        while (*path == '^') {
            if (out.parent_prefix == 255)
                return false;

            ++out.parent_prefix;
            ++path;
        }

        if (*path == '\\') {
            out.root = true;

            ++path;
        }

        while (*path != '\0') {
            if (out.count == max_aml_name_segments)
                return false;

            u32  length  = 0;
            char text[4] = {'_', '_', '_', '_'};

            while (path[length] != '\0' && path[length] != '.') {
                if (length == 4)
                    return false;

                const u8 character = static_cast<u8>(path[length]);

                if (length == 0 ? !lead_name_char(character) : !name_char(character))
                    return false;

                text[length++] = static_cast<char>(character);
            }

            if (length == 0)
                return false;

            out.segment[out.count++] = name_segment(text[0], text[1], text[2], text[3]);

            path += length;

            if (*path == '.') {
                ++path;

                if (*path == '\0')
                    return false;
            }
            else if (*path != '\0') {
                return false;
            }
        }

        return out.count != 0 || out.parent_prefix != 0 || out.root;
    }

    bool interpreter::find(const char* path, node_id scope, node_id& out) {
        name_ref reference;

        if (!parse_path(path, reference))
            return false;

        out = resolve_lookup(reference, scope);

        return out != no_node;
    }

    bool interpreter::follow_alias_node(node_id id, node_id& out) {
        out = follow_alias(id, 0);

        return out != no_node;
    }

    bool interpreter::buffer_mutable(value_id id, u8*& data, u32& length) {
        data = writable_buffer(id, length);

        return data != nullptr || length == 0;
    }

    bool interpreter::evaluate(const char* path, value_id& out) {
        reset_evaluation();

        name_ref reference;

        if (!parse_path(path, reference))
            return fail(aml_error::name);

        const node_id resolved = resolve_lookup(reference, root());
        if (resolved == no_node)
            return fail(aml_error::missing_object);

        return evaluate_node(resolved, out);
    }

    bool interpreter::evaluate_node(node_id id, value_id& out) {
        return evaluate_node(id, nullptr, 0, out);
    }

    bool interpreter::evaluate_node(node_id id, const value_id* arguments, usz count,
                                    value_id& out) {
        if (id >= _node_count)
            return fail(aml_error::missing_object);

        const node_id resolved = follow_alias(id, 0);
        if (resolved == no_node)
            return fail(aml_error::missing_object);

        const node& entry = _nodes[resolved];

        if (entry.kind == static_cast<u8>(node_kind::method)) {
            if (count != entry.arg_count)
                return fail(aml_error::method_arguments);

            return execute_method(resolved, arguments, count, out);
        }

        if (entry.kind == static_cast<u8>(node_kind::field)) {
            u64 value = 0;

            if (!read_field(resolved, value))
                return false;

            out = add_integer(value);

            return out != no_value || fail(aml_error::value_limit);
        }

        if (entry.value == no_value)
            return fail(aml_error::missing_object);

        out = dereference(entry.value);

        return out != no_value || fail(aml_error::missing_object);
    }

    bool interpreter::step() {
        if (_steps == max_aml_eval_steps)
            return fail(aml_error::execution_limit);

        ++_steps;

        return true;
    }

    bool interpreter::evaluate_s5(value_id& package) {
        reset_evaluation();

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
