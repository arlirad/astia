#include <astia/mapping.hpp>
#include <astia/optional.hpp>
#include <astia/fs.hpp>

#include "../src/fs/mapping.hpp"
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

ASTIA_TEST(file_mode_mapping_preserves_type_and_permissions) {
    const auto value = astia::mapper<astia::fs::file_mode>::from(0100754);
    ASTIA_CHECK(value.has_value);
    ASTIA_CHECK(value.value.is_regular());
    ASTIA_CHECK(value.value.owner_allows(astia::fs::permission_bits::read));
    ASTIA_CHECK(!value.value.group_allows(astia::fs::permission_bits::write));
    ASTIA_CHECK(astia::mapper<astia::fs::file_mode>::to(value.value) == 0100754);
}

ASTIA_TEST(file_mode_permission_decisions_and_chmod_mask) {
    const astia::fs::file_mode mode(astia::fs::file_type::directory,
                                    astia::fs::file_permissions(0750));
    ASTIA_CHECK(mode.is_directory());
    ASTIA_CHECK(mode.owner_allows(astia::fs::permission_bits::write));
    ASTIA_CHECK(mode.group_allows(astia::fs::permission_bits::execute));
    ASTIA_CHECK(!mode.other_allows(astia::fs::permission_bits::read));
    ASTIA_CHECK(mode.chmod(astia::fs::file_permissions(0777)).permissions().raw() == 0777);
}
