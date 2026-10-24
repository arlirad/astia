#include <astia/mapping.hpp>
#include <astia/optional.hpp>

#include "test.hpp"

namespace {

    enum class flags : astia::u8 {
        none = 0,
        read = 1u << 0,
        write = 1u << 1,
    };

    using flags_mapper = astia::bitmask_mapper<flags, 0x3>;

}

ASTIA_TEST(mapping_accepts_valid_bitmask) {
    const auto value = flags_mapper::from(0x3);
    ASTIA_CHECK(value.has_value);
    ASTIA_CHECK(static_cast<astia::u8>(value.value) == 0x3);
}

ASTIA_TEST(mapping_rejects_unknown_bits) {
    const auto value = flags_mapper::from(0x4);
    ASTIA_CHECK(!value.has_value);
    ASTIA_CHECK(value.error == astia::error_t::einval);
}
