#include "prt.hpp"

namespace astia::acpi::detail {
    namespace {
        constexpr u32 eisa_pnp0a03        = 0x030AD041;
        constexpr u32 eisa_pnp0a08        = 0x080AD041;
        constexpr u8  irq_tag             = 0x22;
        constexpr u8  extended_irq_tag    = 0x89;
        constexpr u8  word_address_tag    = 0x88;
        constexpr u8  bus_number_resource = 2;
        constexpr usz max_root_bridges    = 8;
        constexpr u32 max_bridge_depth    = 8;
        constexpr usz max_descriptors     = 128;

        constexpr u8 preferred_irqs[] = {
            10, 11, 9, 5, 12, 15, 14, 13, 7, 6, 4, 3,
        };

        constexpr u32 segment(char a, char b, char c, char d) {
            return static_cast<u32>(static_cast<u8>(a)) |
                   (static_cast<u32>(static_cast<u8>(b)) << 8) |
                   (static_cast<u32>(static_cast<u8>(c)) << 16) |
                   (static_cast<u32>(static_cast<u8>(d)) << 24);
        }

        struct descriptor {
            u32  body   = 0;
            u32  length = 0;
            u8   tag    = 0;
            bool large  = false;
        };

        struct irq_resource {
            bool extended = false;
            u32  mask     = 0;
            u32  first    = 0;
            u32  count    = 0;
        };

        struct root_bridge {
            node_id id        = no_node;
            u32     bus       = 0;
            bool    bus_known = false;
        };

        bool next_descriptor(const u8* data, u32 length, u32& cursor, descriptor& out);

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
                            u32 cursor = 0;

                            for (usz i = 0; i < max_descriptors && cursor < length; ++i) {
                                descriptor current{};

                                if (!next_descriptor(data, length, cursor, current))
                                    break;

                                if (current.large && current.tag == word_address_tag &&
                                    current.length >= 13 &&
                                    data[current.body] == bus_number_resource) {
                                    out[count].bus = load_u16(data + current.body + 5);
                                    out[count].bus_known = true;

                                    break;
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

        bool next_descriptor(const u8* data, u32 length, u32& cursor, descriptor& out) {
            if (data == nullptr || cursor >= length)
                return false;

            const u8 tag = data[cursor];

            if ((tag & 0x80u) == 0) {
                const u32 body_length = tag & 0x07u;

                if (body_length > length - cursor - 1)
                    return false;

                out.tag    = tag;
                out.body   = cursor + 1;
                out.length = body_length;
                out.large  = false;
                cursor += 1 + body_length;

                return true;
            }

            if (length - cursor < 3)
                return false;

            const u32 body_length = load_u16(data + cursor + 1);

            if (body_length > length - cursor - 3)
                return false;

            out.tag    = tag;
            out.body   = cursor + 3;
            out.length = body_length;
            out.large  = true;
            cursor += 3 + body_length;

            return true;
        }

        bool descriptor_at(const u8* data, u32 length, u32 index, descriptor& out) {
            u32 cursor  = 0;
            u32 current = 0;

            while (cursor < length) {
                descriptor candidate{};

                if (!next_descriptor(data, length, cursor, candidate))
                    return false;

                if (current == index) {
                    out = candidate;

                    return true;
                }

                ++current;
            }

            return false;
        }

        bool irq_of(const u8* data, const descriptor& value, irq_resource& out) {
            if (!value.large && (value.tag == irq_tag || value.tag == 0x23)) {
                const u32 minimum = value.tag == 0x23 ? 3 : 2;

                if (value.length < minimum)
                    return false;

                out.extended = false;
                out.mask     = static_cast<u32>(data[value.body]) |
                           (static_cast<u32>(data[value.body + 1]) << 8);
                out.count = out.mask == 0 ? 0 : 1;

                return true;
            }

            if (value.large && value.tag == extended_irq_tag) {
                if (value.length < 2)
                    return false;

                const u32 count = data[value.body + 1];

                if (count > (value.length - 2) / 4)
                    return false;

                out.extended = true;
                out.count    = count;
                out.first    = count == 0 ? 0 : load_u32(data + value.body + 2);

                return true;
            }

            return false;
        }

        bool selected_irq(const u8* data, u32 length, u32 index, u8& irq) {
            descriptor value{};

            if (!descriptor_at(data, length, index, value))
                return false;

            irq_resource resource{};

            if (!irq_of(data, value, resource))
                return false;

            if (resource.extended) {
                if (resource.count != 1 || resource.first == 0 || resource.first > 15)
                    return false;

                irq = static_cast<u8>(resource.first);

                return true;
            }

            u32 mask   = resource.mask & 0xFFFFu;
            u32 chosen = 0;
            u32 bits   = 0;

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

        bool possible_irq(const u8* data, u32 length, u32 index, u8& irq) {
            descriptor value{};

            if (!descriptor_at(data, length, index, value))
                return false;

            irq_resource resource{};

            if (!irq_of(data, value, resource))
                return false;

            if (resource.extended) {
                for (u8 candidate : preferred_irqs) {
                    for (u32 i = 0; i < resource.count; ++i) {
                        if (load_u32(data + value.body + 2 + i * 4) == candidate) {
                            irq = candidate;

                            return true;
                        }
                    }
                }

                for (u32 i = 0; i < resource.count; ++i) {
                    const u32 candidate = load_u32(data + value.body + 2 + i * 4);

                    if (candidate != 0 && candidate <= 15) {
                        irq = static_cast<u8>(candidate);

                        return true;
                    }
                }

                return false;
            }

            return preferred_possible(resource.mask & 0xFFFFu, irq);
        }

        bool patch_irq(u8* data, u32 length, u32 index, u8 irq) {
            descriptor value{};

            if (irq == 0 || irq > 15 || !descriptor_at(data, length, index, value))
                return false;

            if (!value.large && (value.tag == irq_tag || value.tag == 0x23)) {
                const u32 minimum = value.tag == 0x23 ? 3 : 2;

                if (value.length < minimum)
                    return false;

                const u16 mask = static_cast<u16>(1u << irq);

                data[value.body]     = static_cast<u8>(mask);
                data[value.body + 1] = static_cast<u8>(mask >> 8);

                return true;
            }

            if (value.large && value.tag == extended_irq_tag) {
                if (value.length < 6 || data[value.body + 1] != 1)
                    return false;

                const u32 encoded = irq;
                const u32 body    = value.body + 2;

                data[body]     = static_cast<u8>(encoded);
                data[body + 1] = static_cast<u8>(encoded >> 8);
                data[body + 2] = static_cast<u8>(encoded >> 16);
                data[body + 3] = static_cast<u8>(encoded >> 24);

                return true;
            }

            return false;
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

            if (selected_irq(data, length, index, irq))
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

            u8 possible = 0;

            if (!possible_irq(prs_data, prs_length, index, possible)) {
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

            if (!aml.buffer_mutable(copy, mutable_data, mutable_length) ||
                !patch_irq(mutable_data, mutable_length, index, possible)) {
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

            if (selected_irq(data, length, index, irq))
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

        bool saw_prt   = false;
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

            saw_prt = true;

            prt_error route_error = prt_error::none;

            if (route_from_prt(aml, bridges[i].id, prt_node, device, function, pin, out,
                               route_error))
                return true;

            error = route_error;

            return false;
        }

        if (!saw_known)
            error = prt_error::no_root_bridge;
        else
            error = saw_prt ? prt_error::no_route : prt_error::no_prt;

        return false;
    }
} // namespace astia::acpi::detail
