#include <astia/mapping.hpp>
#include <astia/optional.hpp>
#include <astia/fs.hpp>

#include "../src/fs/mapping.hpp"
#include "../src/proc/mapping.hpp"
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

ASTIA_TEST(permission_bits_require_all_requested_bits) {
    const astia::fs::file_mode mode(astia::fs::file_type::regular,
                                    astia::fs::file_permissions(0644));
    ASTIA_CHECK(mode.owner_allows(astia::fs::permission_bits::read));
    ASTIA_CHECK(!mode.owner_allows(static_cast<astia::fs::permission_bits>(5)));
    ASTIA_CHECK(mode.group_allows(astia::fs::permission_bits::read));
    ASTIA_CHECK(!mode.group_allows(astia::fs::permission_bits::write));
    ASTIA_CHECK(mode.other_allows(astia::fs::permission_bits::read));
}

ASTIA_TEST(process_domain_mappers_reject_invalid_values) {
    const auto pid = astia::mapper<astia::pid_t>::from(0);
    const auto group = astia::mapper<astia::pgrp_t>::from(-1);
    const auto signal = astia::mapper<astia::signal_number>::from(32);
    ASTIA_CHECK(!pid.has_value && pid.error == astia::error_t::einval);
    ASTIA_CHECK(!group.has_value && group.error == astia::error_t::einval);
    ASTIA_CHECK(!signal.has_value && signal.error == astia::error_t::einval);
}

ASTIA_TEST(process_domain_mappers_reject_undefined_values) {
    ASTIA_CHECK(!astia::mapper<astia::signal_number>::from(-1).has_value);
    ASTIA_CHECK(!astia::mapper<astia::signal_number>::from(16).has_value);
    ASTIA_CHECK(!astia::mapper<astia::signal_number>::from(0).has_value);
    ASTIA_CHECK(!astia::mapper<astia::pid_t>::from(-1).has_value);
    ASTIA_CHECK(!astia::mapper<astia::pid_t>::from(0x100000000ll).has_value);
    ASTIA_CHECK(!astia::mapper<astia::pgrp_t>::from(0).has_value);
    ASTIA_CHECK(!astia::mapper<astia::pgrp_t>::from(0x100000000ll).has_value);
}

ASTIA_TEST(descriptor_mapper_keeps_abi_integer_at_boundary) {
    const auto valid = astia::mapper<astia::fd_t>::from(7);
    const auto negative = astia::mapper<astia::fd_t>::from(-1);
    const auto too_large = astia::mapper<astia::fd_t>::from(64);
    const auto last = astia::mapper<astia::fd_t>::from(astia::proc::max_process_fds - 1);
    ASTIA_CHECK(valid.has_value && valid.value.value == 7);
    ASTIA_CHECK(!negative.has_value && negative.error == astia::error_t::ebadf);
    ASTIA_CHECK(!too_large.has_value && too_large.error == astia::error_t::ebadf);
    ASTIA_CHECK(last.has_value && last.value.value == astia::proc::max_process_fds - 1);
}
