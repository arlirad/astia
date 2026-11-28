#include "resources.hpp"

#include "tables.hpp"

namespace astia::acpi::detail {
    namespace {
        bool in_bounds(span<const u8> bytes, const resource_descriptor& descriptor) {
            return descriptor.body <= bytes.size() &&
                   descriptor.length <= bytes.size() - descriptor.body;
        }

        void store_u32(u8* bytes, u32 value) {
            bytes[0] = static_cast<u8>(value);
            bytes[1] = static_cast<u8>(value >> 8);
            bytes[2] = static_cast<u8>(value >> 16);
            bytes[3] = static_cast<u8>(value >> 24);
        }
    } // namespace

    resource_status resource_iterator::next(resource_descriptor& out) {
        if (_done)
            return _terminal;

        if (_bytes.null() && _bytes.size() != 0)
            return finish(resource_status::malformed);

        if (_cursor >= _bytes.size())
            return finish(resource_status::end_of_data);

        const usz remaining = _bytes.size() - _cursor;
        const u8  tag       = _bytes[_cursor];

        if ((tag & 0x80u) == 0) {
            const usz body_length = tag & 0x07u;

            if (body_length + 1 > remaining)
                return finish(resource_status::malformed);

            if (tag == end_tag) {
                out.tag    = tag;
                out.body   = _cursor + 1;
                out.length = body_length;
                out.large  = false;
                _cursor += 1 + body_length;

                return finish(resource_status::end_tag);
            }

            if (_count >= max_resource_descriptors)
                return finish(resource_status::malformed);

            out.tag    = tag;
            out.body   = _cursor + 1;
            out.length = body_length;
            out.large  = false;
            _cursor += 1 + body_length;

            ++_count;

            return resource_status::descriptor;
        }

        if (remaining < 3)
            return finish(resource_status::malformed);

        const usz body_length = load_u16(_bytes.data() + _cursor + 1);

        if (body_length > remaining - 3)
            return finish(resource_status::malformed);

        if (_count >= max_resource_descriptors)
            return finish(resource_status::malformed);

        out.tag    = tag;
        out.body   = _cursor + 3;
        out.length = body_length;
        out.large  = true;
        _cursor += 3 + body_length;

        ++_count;

        return resource_status::descriptor;
    }

    bool descriptor_at(span<const u8> bytes, usz index, resource_descriptor& out) {
        resource_iterator iterator(bytes);

        usz current = 0;

        while (true) {
            resource_descriptor candidate{};

            if (iterator.next(candidate) != resource_status::descriptor)
                return false;

            if (current == index) {
                out = candidate;

                return true;
            }

            ++current;
        }
    }

    namespace {
        resource_template_status template_end(span<const u8> bytes,
                                              usz&           checksum_offset) {
            resource_iterator iterator(bytes);

            while (true) {
                resource_descriptor descriptor{};

                const resource_status status = iterator.next(descriptor);

                if (status == resource_status::descriptor)
                    continue;

                if (status == resource_status::end_of_data)
                    return resource_template_status::missing_end_tag;

                if (status != resource_status::end_tag)
                    return resource_template_status::malformed;

                if (descriptor.body >= bytes.size() ||
                    bytes.size() - descriptor.body != 1)
                    return resource_template_status::malformed;

                checksum_offset = descriptor.body;

                return resource_template_status::valid;
            }
        }
    } // namespace

    resource_template_status validate_template(span<const u8> bytes) {
        usz checksum_offset = 0;

        const resource_template_status status = template_end(bytes, checksum_offset);

        if (status != resource_template_status::valid)
            return status;

        const u8 checksum = bytes[checksum_offset];
        if (checksum == 0)
            return resource_template_status::valid;

        u8 sum = 0;

        for (usz i = 0; i < checksum_offset; ++i) {
            sum = static_cast<u8>(sum + bytes[i]);
        }

        sum = static_cast<u8>(sum + checksum);

        return sum == 0 ? resource_template_status::valid
                        : resource_template_status::checksum_mismatch;
    }

    bool refresh_template_checksum(span<u8> bytes) {
        usz checksum_offset = 0;

        if (template_end(bytes, checksum_offset) != resource_template_status::valid)
            return false;

        if (bytes[checksum_offset] == 0)
            return true;

        u8 sum = 0;

        for (usz i = 0; i < checksum_offset; ++i) {
            sum = static_cast<u8>(sum + bytes[i]);
        }

        bytes[checksum_offset] = static_cast<u8>(0 - sum);

        return true;
    }

    bool decode_irq(span<const u8> bytes, const resource_descriptor& descriptor,
                    interrupt_resource& out) {
        if (!in_bounds(bytes, descriptor))
            return false;

        out = interrupt_resource{};

        if (!descriptor.large &&
            (descriptor.tag == small_irq_tag || descriptor.tag == small_irq_flags_tag)) {
            const usz expected = descriptor.tag == small_irq_flags_tag ? 3 : 2;

            if (descriptor.length != expected)
                return false;

            out.mask = load_u16(bytes.data() + descriptor.body);

            if (descriptor.tag == small_irq_flags_tag)
                out.flags = bytes[descriptor.body + 2];

            return true;
        }

        if (!descriptor.large || descriptor.tag != extended_interrupt_tag)
            return false;

        if (descriptor.length < 6)
            return false;

        const u32 count = bytes[descriptor.body + 1];

        if (count == 0 || count > (descriptor.length - 2) / 4)
            return false;

        out.extended     = true;
        out.flags        = bytes[descriptor.body];
        out.vectors      = descriptor.body + 2;
        out.vector_count = count;

        return true;
    }

    u32 irq_vector(span<const u8> bytes, const interrupt_resource& resource, usz index) {
        if (!resource.extended || resource.vectors > bytes.size())
            return 0;

        const usz available = bytes.size() - resource.vectors;

        if (index >= resource.vector_count || available < 4 ||
            index > (available - 4) / 4)
            return 0;

        return load_u32(bytes.data() + resource.vectors + index * 4);
    }

    bool patch_irq(span<u8> bytes, const resource_descriptor& descriptor, u8 irq) {
        if (irq == 0 || irq > 15)
            return false;

        interrupt_resource resource{};

        if (!decode_irq(bytes, descriptor, resource))
            return false;

        if (resource.extended) {
            if (resource.vector_count != 1)
                return false;

            store_u32(bytes.data() + resource.vectors, irq);

            return true;
        }

        const u16 mask = static_cast<u16>(1u << irq);

        bytes[descriptor.body]     = static_cast<u8>(mask);
        bytes[descriptor.body + 1] = static_cast<u8>(mask >> 8);

        return true;
    }

    bool decode_bus_number(span<const u8> bytes, const resource_descriptor& descriptor,
                           bus_number_range& out) {
        if (!descriptor.large || descriptor.tag != word_address_tag ||
            descriptor.length < 13 || !in_bounds(bytes, descriptor) ||
            bytes[descriptor.body] != bus_number_resource)
            return false;

        const u8 general_flags = bytes[descriptor.body + 1];

        if ((general_flags & 0xF0u) != 0)
            return false;

        if (bytes[descriptor.body + 2] != 0)
            return false;

        const u16 granularity = load_u16(bytes.data() + descriptor.body + 3);
        const u16 minimum     = load_u16(bytes.data() + descriptor.body + 5);
        const u16 maximum     = load_u16(bytes.data() + descriptor.body + 7);
        const u16 length      = load_u16(bytes.data() + descriptor.body + 11);

        if ((static_cast<u32>(granularity) & (static_cast<u32>(granularity) + 1)) != 0)
            return false;

        if (minimum > maximum)
            return false;

        const bool min_fixed = (general_flags & 0x04u) != 0;
        const bool max_fixed = (general_flags & 0x08u) != 0;

        if (length == 0) {
            if (min_fixed && max_fixed)
                return false;

            if (min_fixed && (minimum & granularity) != 0)
                return false;

            if (max_fixed && (maximum & granularity) != granularity)
                return false;
        }
        else {
            if (min_fixed != max_fixed)
                return false;

            const u32 span = static_cast<u32>(maximum) - minimum + 1;

            if (min_fixed) {
                if (granularity != 0 || length != span)
                    return false;
            }
            else {
                if ((length & granularity) != 0 || length > span)
                    return false;
            }
        }

        out.minimum = minimum;
        out.maximum = maximum;
        out.length  = length;

        return true;
    }
} // namespace astia::acpi::detail
