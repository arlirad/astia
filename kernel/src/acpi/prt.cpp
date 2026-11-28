#include "prt.hpp"

#include "resources.hpp"

namespace astia::acpi::detail {
    namespace {
        constexpr u32 eisa_pnp0a03     = 0x030AD041;
        constexpr u32 eisa_pnp0a08     = 0x080AD041;
        constexpr usz max_root_bridges = 8;
        constexpr u32 max_bridge_depth = 8;

        constexpr u8 preferred_irqs[] = {
            10, 11, 9, 5, 12, 15, 14, 13, 7, 6, 4, 3,
        };

        constexpr u32 segment(char a, char b, char c, char d) {
            return static_cast<u32>(static_cast<u8>(a)) |
                   (static_cast<u32>(static_cast<u8>(b)) << 8) |
                   (static_cast<u32>(static_cast<u8>(c)) << 16) |
                   (static_cast<u32>(static_cast<u8>(d)) << 24);
        }

        struct root_bridge {
            node_id id        = no_node;
            u32     bus       = 0;
            bool    bus_known = false;
        };

        bool name_equals(const char* text, u32 length, const char (&expected)[8]) {
            if (text == nullptr || length != 7)
                return false;

            for (u32 i = 0; i < 7; ++i) {
                if (text[i] != expected[i])
                    return false;
            }

            return true;
        }

        bool evaluate_child(interpreter& aml, node_id parent, u32 name, value_id& out) {
            const node_id child = aml.child(parent, name);
            if (child == no_node)
                return false;

            return aml.evaluate_node(child, out);
        }

        bool is_root_bridge(interpreter& aml, node_id device) {
            value_id hid = no_value;

            if (!evaluate_child(aml, device, segment('_', 'H', 'I', 'D'), hid))
                return false;

            u64 integer = 0;

            if (aml.integer(hid, integer))
                return integer == eisa_pnp0a03 || integer == eisa_pnp0a08;

            const char* text = nullptr;

            u32 length = 0;

            if (!aml.string(hid, text, length))
                return false;

            return name_equals(text, length, "PNP0A03") ||
                   name_equals(text, length, "PNP0A08");
        }

        bool collect_root_bridges(interpreter& aml, node_id parent, u32 depth,
                                  root_bridge* out, usz capacity, usz& count) {
            for (node_id child = aml.first_child(parent); child != no_node;
                 child         = aml.next_sibling(child)) {
                const struct node* entry = aml.node_at(child);
                if (entry == nullptr)
                    return false;

                const auto kind = static_cast<node_kind>(entry->kind);

                if ((kind == node_kind::device || kind == node_kind::scope) &&
                    is_root_bridge(aml, child)) {
                    if (count == capacity)
                        return false;

                    out[count].id        = child;
                    out[count].bus       = 0;
                    out[count].bus_known = false;

                    value_id crs = no_value;

                    if (evaluate_child(aml, child, segment('_', 'C', 'R', 'S'), crs)) {
                        const u8* data = nullptr;

                        u32 length = 0;

                        if (aml.buffer(crs, data, length)) {
                            const span<const u8> template_bytes(data, length);

                            if (validate_template(template_bytes) ==
                                resource_template_status::valid) {
                                resource_iterator resources(template_bytes);

                                for (usz i = 0; i < max_resource_descriptors; ++i) {
                                    resource_descriptor   current{};
                                    const resource_status status =
                                        resources.next(current);

                                    if (status != resource_status::descriptor)
                                        break;

                                    bus_number_range range{};

                                    if (decode_bus_number(template_bytes, current,
                                                          range)) {
                                        out[count].bus       = range.minimum;
                                        out[count].bus_known = true;

                                        break;
                                    }
                                }
                            }
                        }
                    }

                    ++count;
                }

                if (depth < max_bridge_depth &&
                    (kind == node_kind::scope || kind == node_kind::device)) {
                    if (!collect_root_bridges(aml, child, depth + 1, out, capacity,
                                              count))
                        return false;
                }
            }

            return true;
        }

        bool selected_irq(span<const u8> bytes, u32 index, u8& irq) {
            resource_descriptor value{};

            if (!descriptor_at(bytes, index, value))
                return false;

            interrupt_resource resource{};

            if (!decode_irq(bytes, value, resource))
                return false;

            if (resource.extended) {
                if (resource.vector_count != 1)
                    return false;

                const u32 vector = irq_vector(bytes, resource, 0);

                if (vector == 0 || vector > 15)
                    return false;

                irq = static_cast<u8>(vector);

                return true;
            }

            const u32 mask   = resource.mask & 0xFFFFu;
            u32       chosen = 0;
            u32       bits   = 0;

            for (u32 bit = 0; bit < 16; ++bit) {
                if ((mask & (1u << bit)) != 0) {
                    ++bits;

                    if (chosen == 0)
                        chosen = bit;
                }
            }

            if (bits != 1 || chosen == 0 || chosen > 15)
                return false;

            irq = static_cast<u8>(chosen);

            return true;
        }

        bool preferred_possible(u32 mask, u8& irq) {
            for (u8 candidate : preferred_irqs) {
                if ((mask & (1u << candidate)) != 0) {
                    irq = candidate;

                    return true;
                }
            }

            return false;
        }

        bool possible_irq(span<const u8> bytes, u32 index, u8& irq) {
            resource_descriptor value{};

            if (!descriptor_at(bytes, index, value))
                return false;

            interrupt_resource resource{};

            if (!decode_irq(bytes, value, resource))
                return false;

            if (resource.extended) {
                for (u8 candidate : preferred_irqs) {
                    for (usz i = 0; i < resource.vector_count; ++i) {
                        if (irq_vector(bytes, resource, i) == candidate) {
                            irq = candidate;

                            return true;
                        }
                    }
                }

                for (usz i = 0; i < resource.vector_count; ++i) {
                    const u32 candidate = irq_vector(bytes, resource, i);

                    if (candidate != 0 && candidate <= 15) {
                        irq = static_cast<u8>(candidate);

                        return true;
                    }
                }

                return false;
            }

            return preferred_possible(resource.mask & 0xFFFFu, irq);
        }

        bool resolve_link(interpreter& aml, node_id link, u32 index, u8& irq,
                          prt_error& error) {
            node_id source = link;

            if (!aml.follow_alias_node(source, source))
                return false;

            value_id crs = no_value;

            if (!evaluate_child(aml, source, segment('_', 'C', 'R', 'S'), crs)) {
                error = prt_error::no_crs;

                return false;
            }

            const u8* data = nullptr;

            u32 length = 0;

            if (!aml.buffer(crs, data, length)) {
                error = prt_error::malformed_crs;

                return false;
            }

            const span<const u8> crs_template(data, length);

            if (validate_template(crs_template) != resource_template_status::valid) {
                error = prt_error::malformed_crs;

                return false;
            }

            if (selected_irq(crs_template, index, irq))
                return true;

            value_id prs = no_value;

            if (!evaluate_child(aml, source, segment('_', 'P', 'R', 'S'), prs)) {
                error = prt_error::link_unselected;

                return false;
            }

            const u8* prs_data = nullptr;

            u32 prs_length = 0;

            if (!aml.buffer(prs, prs_data, prs_length)) {
                error = prt_error::malformed_crs;

                return false;
            }

            const span<const u8> prs_template(prs_data, prs_length);

            if (validate_template(prs_template) != resource_template_status::valid) {
                error = prt_error::malformed_crs;

                return false;
            }

            u8 possible = 0;

            if (!possible_irq(prs_template, index, possible)) {
                error = prt_error::link_unselected;

                return false;
            }

            const value_id copy = aml.add_buffer(data, length);
            if (copy == no_value) {
                error = prt_error::malformed_crs;

                return false;
            }

            u8* mutable_data = nullptr;

            u32 mutable_length = 0;

            if (!aml.buffer_mutable(copy, mutable_data, mutable_length)) {
                error = prt_error::malformed_crs;

                return false;
            }

            const span<u8>      mutable_template(mutable_data, mutable_length);
            resource_descriptor target{};

            if (!descriptor_at(mutable_template, index, target) ||
                !patch_irq(mutable_template, target, possible) ||
                !refresh_template_checksum(mutable_template)) {
                error = prt_error::malformed_crs;

                return false;
            }

            const node_id srs = aml.child(source, segment('_', 'S', 'R', 'S'));
            if (srs == no_node) {
                error = prt_error::link_unselected;

                return false;
            }

            value_id ignored = no_value;

            if (!aml.evaluate_node(srs, &copy, 1, ignored)) {
                error = prt_error::srs_failed;

                return false;
            }

            crs = no_value;

            if (!evaluate_child(aml, source, segment('_', 'C', 'R', 'S'), crs)) {
                error = prt_error::link_unselected;

                return false;
            }

            if (!aml.buffer(crs, data, length)) {
                error = prt_error::malformed_crs;

                return false;
            }

            const span<const u8> selected_template(data, length);

            if (validate_template(selected_template) != resource_template_status::valid) {
                error = prt_error::malformed_crs;

                return false;
            }

            if (selected_irq(selected_template, index, irq))
                return true;

            error = prt_error::link_unselected;

            return false;
        }

        bool source_name(interpreter& aml, node_id scope, value_id source, node_id& out) {
            const value* value = aml.get(source);
            if (value == nullptr)
                return false;

            const auto kind = static_cast<value_kind>(value->kind);

            if (kind == value_kind::name_reference) {
                name_ref reference{};

                if (!aml.name_reference(source, reference))
                    return false;

                return aml.resolve(reference, scope, out);
            }

            if (kind == value_kind::string) {
                const char* text = nullptr;

                u32 length = 0;

                if (!aml.string(source, text, length) || text == nullptr)
                    return false;

                char path[128]{};

                if (length >= sizeof(path))
                    return false;

                for (u32 i = 0; i < length; ++i) {
                    if (text[i] == '\0')
                        return false;

                    path[i] = text[i];
                }

                return aml.find(path, scope, out);
            }

            return false;
        }

        bool route_from_entry(interpreter& aml, node_id prt_scope, value_id entry_id,
                              u8 device, u8 function, u8 pin, prt_route& out,
                              prt_error& error, bool& wildcard_match) {
            const struct value* entry = aml.get(entry_id);
            if (entry == nullptr)
                return false;

            value_id item = no_value;

            u64 address      = 0;
            u64 entry_pin    = 0;
            u64 source_index = 0;

            if (!aml.package_item(entry_id, 0, item) || !aml.integer(item, address) ||
                !aml.package_item(entry_id, 1, item) || !aml.integer(item, entry_pin) ||
                !aml.package_item(entry_id, 3, item) ||
                !aml.integer(item, source_index)) {
                error = prt_error::malformed_prt;

                return false;
            }

            if (entry_pin > 3 || (address >> 32) != 0) {
                error = prt_error::malformed_prt;

                return false;
            }

            if (entry_pin != static_cast<u64>(pin - 1))
                return true;

            const u64 entry_device   = (address >> 16) & 0xFFFFu;
            const u64 entry_function = address & 0xFFFFu;

            if (entry_device != device)
                return true;

            if (entry_function != 0xFFFFu && entry_function != function)
                return true;

            wildcard_match = entry_function == 0xFFFFu;

            if (!aml.package_item(entry_id, 2, item)) {
                error = prt_error::malformed_prt;

                return false;
            }

            const struct value* source = aml.get(item);
            if (source == nullptr) {
                error = prt_error::malformed_prt;

                return false;
            }

            const auto kind = static_cast<value_kind>(source->kind);

            if (kind == value_kind::integer) {
                u64 value = 0;

                if (!aml.integer(item, value)) {
                    error = prt_error::malformed_prt;

                    return false;
                }

                if (value != 0) {
                    error = prt_error::unsupported_source;

                    return false;
                }

                if (source_index > 0xFF) {
                    error = prt_error::irq_out_of_range;

                    return false;
                }

                out.direct = true;
                out.gsi    = static_cast<u8>(source_index);
                out.irq    = out.gsi == 0 || out.gsi > 15 ? prt_no_irq : out.gsi;
                out.pin    = pin;

                if (out.irq == prt_no_irq) {
                    error = prt_error::irq_out_of_range;

                    return false;
                }

                out.valid = true;

                return true;
            }

            node_id link = no_node;

            if (!source_name(aml, prt_scope, item, link)) {
                error = prt_error::unsupported_source;

                return false;
            }

            if (source_index > 0xFFFFu) {
                error = prt_error::descriptor_out_of_range;

                return false;
            }

            u8 irq = prt_no_irq;

            if (!resolve_link(aml, link, static_cast<u32>(source_index), irq, error))
                return false;

            out.direct = false;
            out.gsi    = 0;
            out.irq    = irq;
            out.pin    = pin;
            out.valid  = irq != prt_no_irq;

            if (!out.valid) {
                error = prt_error::link_unselected;

                return false;
            }

            return true;
        }

        bool route_from_prt(interpreter& aml, node_id bridge, node_id prt_node, u8 device,
                            u8 function, u8 pin, prt_route& out, prt_error& error) {
            value_id package_value = no_value;

            if (!aml.evaluate_node(prt_node, package_value)) {
                error = prt_error::no_prt;

                return false;
            }

            const struct value* package = nullptr;

            if (!aml.package(package_value, package) || package == nullptr) {
                error = prt_error::malformed_prt;

                return false;
            }

            prt_route fallback{};

            bool have_fallback = false;

            value_id item = package->first;

            for (u32 i = 0; i < package->count; ++i) {
                if (item == no_value) {
                    error = prt_error::malformed_prt;

                    return false;
                }

                const struct value* entry = aml.get(item);

                if (entry == nullptr ||
                    entry->kind != static_cast<u8>(value_kind::package)) {
                    error = prt_error::malformed_prt;

                    return false;
                }

                prt_route matched{};
                prt_error match_error = prt_error::none;

                bool wildcard_match = false;

                const bool matched_ok =
                    route_from_entry(aml, bridge, item, device, function, pin, matched,
                                     match_error, wildcard_match);

                if (!matched_ok) {
                    error = match_error;

                    return false;
                }

                if (matched.valid) {
                    if (!wildcard_match) {
                        out = matched;

                        return true;
                    }

                    fallback      = matched;
                    have_fallback = true;
                }

                const struct value* next = aml.get(item);
                if (next == nullptr)
                    break;

                item = next->next;
            }

            if (have_fallback) {
                out = fallback;

                return true;
            }

            error = prt_error::no_route;

            return false;
        }
    } // namespace

    const char* prt_error_text(prt_error error) {
        switch (error) {
        case prt_error::none:
            return "ok";
        case prt_error::no_root_bridge:
            return "PCI root bridge absent";
        case prt_error::no_prt:
            return "_PRT absent";
        case prt_error::malformed_prt:
            return "malformed _PRT";
        case prt_error::no_route:
            return "_PRT route absent";
        case prt_error::unsupported_source:
            return "unsupported _PRT source";
        case prt_error::no_crs:
            return "link _CRS absent";
        case prt_error::malformed_crs:
            return "malformed link _CRS";
        case prt_error::descriptor_out_of_range:
            return "link descriptor index";
        case prt_error::link_unselected:
            return "link IRQ unselected";
        case prt_error::srs_failed:
            return "link _SRS failed";
        case prt_error::irq_out_of_range:
            return "IRQ out of range";
        }

        return "unknown";
    }

    bool resolve_prt_route(interpreter& aml, u8 bus, u8 device, u8 function, u8 pin,
                           prt_route& out, prt_error& error) {
        out   = prt_route{};
        error = prt_error::none;

        if (pin < 1 || pin > 4) {
            error = prt_error::no_route;

            return false;
        }

        aml.reset_evaluation();

        root_bridge bridges[max_root_bridges]{};

        usz bridge_count = 0;

        if (!collect_root_bridges(aml, 0, 0, bridges, max_root_bridges, bridge_count)) {
            error = prt_error::no_root_bridge;

            return false;
        }

        if (bridge_count == 0) {
            error = prt_error::no_root_bridge;

            return false;
        }

        bool saw_known = false;

        for (usz i = 0; i < bridge_count; ++i) {
            if (!bridges[i].bus_known)
                continue;

            saw_known = true;

            if (bridges[i].bus != bus)
                continue;

            const node_id prt_node =
                aml.child(bridges[i].id, segment('_', 'P', 'R', 'T'));
            if (prt_node == no_node)
                continue;

            prt_error route_error = prt_error::none;

            if (route_from_prt(aml, bridges[i].id, prt_node, device, function, pin, out,
                               route_error))
                return true;

            error = route_error;

            return false;
        }

        error = saw_known ? prt_error::no_prt : prt_error::no_root_bridge;

        return false;
    }
} // namespace astia::acpi::detail
