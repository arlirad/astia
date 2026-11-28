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

    bool interpreter::package_item(value_id id, u32 index, value_id& out) {
        const value* package = nullptr;

        if (!as_package(id, package) || index >= package->count ||
            package->first == no_value)
            return false;

        value_id item = package->first;

        for (u32 i = 0; i < index; ++i) {
            const value* entry = get(item);

            if (entry == nullptr || entry->next == no_value)
                return false;

            item = entry->next;
        }

        out = item;

        return true;
    }

    bool interpreter::buffer(value_id id, const u8*& data, u32& length) {
        const value_id resolved = dereference(id);

        if (resolved == no_value ||
            _values[resolved].kind != static_cast<u8>(value_kind::buffer))
            return false;

        data   = _values[resolved].data;
        length = _values[resolved].length;

        return data != nullptr || length == 0;
    }

    bool interpreter::string(value_id id, const char*& data, u32& length) {
        const value_id resolved = dereference(id);

        if (resolved == no_value ||
            _values[resolved].kind != static_cast<u8>(value_kind::string))
            return false;

        data   = reinterpret_cast<const char*>(_values[resolved].data);
        length = _values[resolved].length;

        return data != nullptr || length == 0;
    }

    bool interpreter::name_reference(value_id id, name_ref& out) const {
        if (id >= _value_count ||
            _values[id].kind != static_cast<u8>(value_kind::name_reference))
            return false;

        const u32 reference = _values[id].reference;
        if (reference >= _reference_count)
            return false;

        out = _references[reference];

        return true;
    }

    value_id interpreter::add_buffer(const u8* data, u32 length) {
        if (length > max_aml_buffer_bytes - _buffer_used)
            return no_value;

        u8* storage = _buffer_storage + _buffer_used;

        if (length != 0 && data == nullptr)
            return no_value;

        if (length != 0)
            __builtin_memcpy(storage, data, length);

        _buffer_used += length;

        const value_id id = add_value(value_kind::buffer);

        if (id == no_value)
            return no_value;

        _values[id].data     = storage;
        _values[id].writable = storage;
        _values[id].length   = length;

        return id;
    }

    u8* interpreter::writable_buffer(value_id id, u32& length) {
        const value_id resolved = dereference(id);

        if (resolved == no_value ||
            _values[resolved].kind != static_cast<u8>(value_kind::buffer))
            return nullptr;

        value& entry = _values[resolved];

        length = entry.length;

        if (entry.writable != nullptr)
            return entry.writable;

        if (id < _static_values) {
            if (entry.length > max_aml_persistent_buffer_bytes - _persistent_buffer_used)
                return nullptr;

            u8* storage = _persistent_buffer_storage + _persistent_buffer_used;

            if (entry.length != 0)
                __builtin_memcpy(storage, entry.data, entry.length);

            _persistent_buffer_used += entry.length;

            entry.writable = storage;
            entry.data     = storage;

            return storage;
        }

        if (entry.length > max_aml_buffer_bytes - _buffer_used)
            return nullptr;

        u8* storage = _buffer_storage + _buffer_used;

        if (entry.length != 0)
            __builtin_memcpy(storage, entry.data, entry.length);

        _buffer_used += entry.length;

        entry.writable = storage;
        entry.data     = storage;

        return storage;
    }

    bool interpreter::read_field(node_id id, u64& out) {
        if (id >= _node_count)
            return fail(aml_error::missing_object);

        node& entry = _nodes[id];

        if (entry.kind != static_cast<u8>(node_kind::field))
            return fail(aml_error::wrong_type);

        if (entry.source != no_value) {
            if (entry.source >= _value_count)
                return fail(aml_error::missing_object);

            const u8* data = _values[entry.source].data;
            if (data == nullptr)
                return fail(aml_error::missing_object);

            const u32 length = _values[entry.source].length;

            if (entry.bit_width > 64 ||
                static_cast<u64>(entry.bit_offset) + entry.bit_width >
                    static_cast<u64>(length) * 8)
                return fail(aml_error::invalid_value);

            u64 value = 0;

            for (u32 bit = 0; bit < entry.bit_width; ++bit) {
                const u32 position = entry.bit_offset + bit;

                if (((data[position / 8] >> (position % 8)) & 1u) != 0)
                    value |= 1ull << bit;
            }

            out = value;

            return true;
        }

        if (entry.region == no_node || entry.region >= _node_count)
            return fail(aml_error::missing_object);

        const node& region = _nodes[entry.region];

        if (entry.bit_offset % 8 != 0 || (entry.bit_width != 8 && entry.bit_width != 16 &&
                                          entry.bit_width != 32 && entry.bit_width != 64))
            return fail(aml_error::unsupported_object);

        const u64 offset = region.address + entry.bit_offset / 8;

        if (region.length != 0 &&
            (entry.bit_offset / 8 > region.length ||
             entry.bit_width / 8 > region.length - entry.bit_offset / 8))
            return fail(aml_error::invalid_value);

        if (_region_io.read == nullptr ||
            !_region_io.read(_region_io.context, region.flags, offset,
                             static_cast<u8>(entry.bit_width), out))
            return fail(aml_error::invalid_value);

        return true;
    }

    bool interpreter::write_field(node_id id, u64 value) {
        if (id >= _node_count)
            return fail(aml_error::missing_object);

        node& entry = _nodes[id];

        if (entry.kind != static_cast<u8>(node_kind::field))
            return fail(aml_error::wrong_type);

        if (entry.source != no_value) {
            u32 length = 0;

            u8* data = writable_buffer(entry.source, length);

            if (data == nullptr)
                return fail(aml_error::missing_object);

            if (entry.bit_width > 64 ||
                static_cast<u64>(entry.bit_offset) + entry.bit_width >
                    static_cast<u64>(length) * 8)
                return fail(aml_error::invalid_value);

            for (u32 bit = 0; bit < entry.bit_width; ++bit) {
                const u32 position = entry.bit_offset + bit;
                const u8  mask     = static_cast<u8>(1u << (position % 8));

                if (((value >> bit) & 1u) != 0)
                    data[position / 8] = static_cast<u8>(data[position / 8] | mask);
                else
                    data[position / 8] = static_cast<u8>(data[position / 8] & ~mask);
            }

            return true;
        }

        if (entry.region == no_node || entry.region >= _node_count)
            return fail(aml_error::missing_object);

        const node& region = _nodes[entry.region];

        if (entry.bit_offset % 8 != 0 || (entry.bit_width != 8 && entry.bit_width != 16 &&
                                          entry.bit_width != 32 && entry.bit_width != 64))
            return fail(aml_error::unsupported_object);

        const u64 offset = region.address + entry.bit_offset / 8;

        if (region.length != 0 &&
            (entry.bit_offset / 8 > region.length ||
             entry.bit_width / 8 > region.length - entry.bit_offset / 8))
            return fail(aml_error::invalid_value);

        if (_region_io.write == nullptr ||
            !_region_io.write(_region_io.context, region.flags, offset,
                              static_cast<u8>(entry.bit_width), value))
            return fail(aml_error::invalid_value);

        return true;
    }

    bool interpreter::read_target(const target& destination, u64& out) {
        if (destination.field != no_node)
            return read_field(destination.field, out);

        if (destination.slot == nullptr) {
            out = 0;

            return true;
        }

        return as_integer(*destination.slot, out);
    }

    bool interpreter::write_target(const target& destination, value_id source) {
        if (destination.field != no_node) {
            u64 value = 0;

            if (!as_integer(source, value))
                return fail(aml_error::wrong_type);

            return write_field(destination.field, value);
        }

        if (destination.slot != nullptr) {
            const value_id resolved = dereference(source);

            if (*destination.slot != no_value && resolved != no_value &&
                *destination.slot < _value_count && resolved < _value_count) {
                value&       target = _values[*destination.slot];
                const value& input  = _values[resolved];

                if (target.kind == static_cast<u8>(value_kind::buffer) &&
                    input.kind == static_cast<u8>(value_kind::buffer)) {
                    u32 capacity = 0;

                    u8* data = writable_buffer(*destination.slot, capacity);

                    if (data != nullptr && input.length <= capacity) {
                        if (input.length != 0)
                            __builtin_memcpy(data, input.data, input.length);

                        target.length = input.length;

                        return true;
                    }
                }
                else if (target.kind == static_cast<u8>(value_kind::integer) &&
                         input.kind == static_cast<u8>(value_kind::integer)) {
                    target.integer = input.integer;

                    return true;
                }
            }

            if (destination.name != no_node && !destination.transient &&
                resolved != no_value && resolved >= _static_values)
                return fail(aml_error::unsupported_object);

            *destination.slot = source;
        }

        return true;
    }
} // namespace astia::acpi::detail
