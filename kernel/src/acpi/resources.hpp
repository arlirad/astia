#pragma once

#include <astia/common.hpp>
#include <astia/span.hpp>

namespace astia::acpi::detail {
    inline constexpr usz max_resource_descriptors = 128;

    inline constexpr u8 small_irq_tag          = 0x22;
    inline constexpr u8 small_irq_flags_tag    = 0x23;
    inline constexpr u8 word_address_tag       = 0x88;
    inline constexpr u8 extended_interrupt_tag = 0x89;
    inline constexpr u8 end_tag                = 0x79;
    inline constexpr u8 bus_number_resource    = 2;

    enum class resource_status : u8 {
        descriptor,
        end_tag,
        end_of_data,
        malformed,
    };

    enum class resource_template_status : u8 {
        valid,
        missing_end_tag,
        malformed,
        checksum_mismatch,
    };

    struct resource_descriptor {
        u8   tag    = 0;
        bool large  = false;
        usz  body   = 0;
        usz  length = 0;
    };

    class resource_iterator {
        public:
        resource_iterator() = default;

        explicit resource_iterator(span<const u8> bytes) : _bytes(bytes) {}

        resource_status next(resource_descriptor& out);

        private:
        resource_status finish(resource_status status) {
            _done     = true;
            _terminal = status;

            return status;
        }

        span<const u8>  _bytes{};
        usz             _cursor   = 0;
        usz             _count    = 0;
        bool            _done     = false;
        resource_status _terminal = resource_status::end_of_data;
    };

    bool descriptor_at(span<const u8> bytes, usz index, resource_descriptor& out);

    resource_template_status validate_template(span<const u8> bytes);
    bool                     refresh_template_checksum(span<u8> bytes);

    struct interrupt_resource {
        bool extended     = false;
        u8   flags        = 0;
        u32  mask         = 0;
        usz  vectors      = 0;
        u32  vector_count = 0;
    };

    bool decode_irq(span<const u8> bytes, const resource_descriptor& descriptor,
                    interrupt_resource& out);
    u32  irq_vector(span<const u8> bytes, const interrupt_resource& resource, usz index);
    bool patch_irq(span<u8> bytes, const resource_descriptor& descriptor, u8 irq);

    struct bus_number_range {
        u16 minimum = 0;
        u16 maximum = 0;
        u16 length  = 0;
    };

    bool decode_bus_number(span<const u8> bytes, const resource_descriptor& descriptor,
                           bus_number_range& out);
} // namespace astia::acpi::detail
