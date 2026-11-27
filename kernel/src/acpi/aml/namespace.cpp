#include "aml_internal.hpp"

namespace astia::acpi::detail {
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

        node_id start = base;

        while (true) {
            node_id current = start;

            bool found = true;

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

        const node_id resolved = resolve_lookup(reference, root());

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
} // namespace astia::acpi::detail
