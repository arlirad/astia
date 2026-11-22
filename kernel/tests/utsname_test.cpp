#include <astia/api/syscalls.h>
#include <astia/arch/platform.hpp>

#include "support/test.hpp"
#include "utsname.hpp"

#include <stddef.h>

static_assert(sizeof(astia_utsname) == 325);
static_assert(offsetof(astia_utsname, sysname) == 0);
static_assert(offsetof(astia_utsname, nodename) == 65);
static_assert(offsetof(astia_utsname, release) == 130);
static_assert(offsetof(astia_utsname, version) == 195);
static_assert(offsetof(astia_utsname, machine) == 260);

static bool field_is_terminated_and_zero_padded(const char* field, astia::usize size) {
    astia::usize i = 0;

    while (i < size && field[i] != '\0')
        ++i;
    if (i == size)
        return false;
    while (++i < size) {
        if (field[i] != '\0')
            return false;
    }
    return true;
}

static bool field_equals(const char* field, astia::usize size, const char* value) {
    for (astia::usize i = 0; i < size; ++i) {
        if (field[i] != value[i])
            return false;
        if (value[i] == '\0')
            return true;
    }
    return false;
}

static bool text_equals(const char* left, const char* right) {
    if (left == nullptr || right == nullptr)
        return left == right;
    for (astia::usize i = 0;; ++i) {
        if (left[i] != right[i])
            return false;
        if (left[i] == '\0')
            return true;
    }
}

ASTIA_TEST(utsname_machine_name_comes_from_the_architecture_port) {
    const char* machine = astia::arch::machine_name();

    ASTIA_CHECK(machine != nullptr);
#if defined(__x86_64__)
    ASTIA_CHECK(text_equals(machine, "x86_64"));
#endif
}

ASTIA_TEST(utsname_fill_rejects_missing_machine_identity) {
    astia_utsname name;

    ASTIA_CHECK(!astia::proc::fill_utsname(name, "1.2.3", "Astia 1.2.3", nullptr));
    ASTIA_CHECK(field_equals(name.machine, sizeof name.machine, ""));
    ASTIA_CHECK(field_equals(name.sysname, sizeof name.sysname, ""));

    ASTIA_CHECK(astia::proc::fill_utsname(name, "1.2.3", "Astia 1.2.3", "aarch64"));
    ASTIA_CHECK(field_equals(name.machine, sizeof name.machine, "aarch64"));
}

ASTIA_TEST(utsname_fill_zeroes_padding_and_sets_identity) {
    astia_utsname name;
    __builtin_memset(&name, 0xA5, sizeof name);

    astia::proc::fill_utsname(name, "1.2.3", "Astia 1.2.3", "x86_64");

    ASTIA_CHECK(field_equals(name.sysname, sizeof name.sysname, "Astia"));
    ASTIA_CHECK(field_equals(name.nodename, sizeof name.nodename, "astia"));
    ASTIA_CHECK(field_equals(name.release, sizeof name.release, "1.2.3"));
    ASTIA_CHECK(field_equals(name.version, sizeof name.version, "Astia 1.2.3"));
    ASTIA_CHECK(field_equals(name.machine, sizeof name.machine, "x86_64"));
    ASTIA_CHECK(field_is_terminated_and_zero_padded(name.sysname, sizeof name.sysname));
    ASTIA_CHECK(field_is_terminated_and_zero_padded(name.nodename, sizeof name.nodename));
    ASTIA_CHECK(field_is_terminated_and_zero_padded(name.release, sizeof name.release));
    ASTIA_CHECK(field_is_terminated_and_zero_padded(name.version, sizeof name.version));
    ASTIA_CHECK(field_is_terminated_and_zero_padded(name.machine, sizeof name.machine));
}

ASTIA_TEST(utsname_fill_truncates_long_fields_and_terminates) {
    astia_utsname name;
    char          long_release[100];
    char          long_version[100];

    for (astia::usize i = 0; i < sizeof long_release; ++i) {
        long_release[i] = 'r';
        long_version[i] = 'v';
    }
    long_release[sizeof long_release - 1] = '\0';
    long_version[sizeof long_version - 1] = '\0';

    astia::proc::fill_utsname(name, long_release, long_version, "x86_64");

    ASTIA_CHECK(name.release[64] == '\0');
    ASTIA_CHECK(name.version[64] == '\0');
    for (astia::usize i = 0; i < 64; ++i) {
        ASTIA_CHECK(name.release[i] == 'r');
        ASTIA_CHECK(name.version[i] == 'v');
    }
}

ASTIA_TEST(utsname_fill_handles_null_version_inputs) {
    astia_utsname name;

    astia::proc::fill_utsname(name, nullptr, nullptr, "x86_64");

    ASTIA_CHECK(name.release[0] == '\0');
    ASTIA_CHECK(name.version[0] == '\0');
    ASTIA_CHECK(field_equals(name.sysname, sizeof name.sysname, "Astia"));
    ASTIA_CHECK(field_equals(name.machine, sizeof name.machine, "x86_64"));
}
