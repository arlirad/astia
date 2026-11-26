if (NOT DEFINED ASTIA_KERNEL_SOURCE_DIR OR
        NOT DEFINED ASTIA_ARCH_INCLUDE_BOUNDARIES_SCRIPT OR
        NOT DEFINED ASTIA_PYTHON OR
        NOT DEFINED ASTIA_CXX_COMPILER)
    message(FATAL_ERROR
            "ASTIA_KERNEL_SOURCE_DIR, ASTIA_ARCH_INCLUDE_BOUNDARIES_SCRIPT, "
            "ASTIA_PYTHON, and ASTIA_CXX_COMPILER are required")
endif ()

if (NOT DEFINED ASTIA_ARCH_INCLUDE_BOUNDARIES_BINARY_DIR)
    set(ASTIA_ARCH_INCLUDE_BOUNDARIES_BINARY_DIR
            "${CMAKE_CURRENT_BINARY_DIR}/arch-include-boundaries")
endif ()

if (NOT ASTIA_AARCH64_CXX_COMPILER OR
        ASTIA_AARCH64_CXX_COMPILER MATCHES "-NOTFOUND$")
    find_program(ASTIA_AARCH64_CXX_COMPILER
            NAMES aarch64-linux-gnu-g++ aarch64-none-elf-g++)
endif ()
if (NOT ASTIA_AARCH64_CXX_COMPILER)
    message(FATAL_ERROR
            "An AArch64 C++ compiler is required for the architecture header "
            "matrix; install g++-aarch64-linux-gnu or set "
            "ASTIA_AARCH64_VIRT_CXX_COMPILER")
endif ()

execute_process(
        COMMAND "${ASTIA_PYTHON}" "${ASTIA_ARCH_INCLUDE_BOUNDARIES_SCRIPT}"
                --root "${ASTIA_KERNEL_SOURCE_DIR}"
                --self-test
                --compile-matrix
                --cxx "${ASTIA_CXX_COMPILER}"
                --aarch64-cxx "${ASTIA_AARCH64_CXX_COMPILER}"
                --build-dir
                "${ASTIA_ARCH_INCLUDE_BOUNDARIES_BINARY_DIR}/matrix"
        RESULT_VARIABLE current_result
        OUTPUT_VARIABLE current_output
        ERROR_VARIABLE current_error)
if (current_result)
    message(FATAL_ERROR
            "current source tree violates the architecture include contract:\n"
            "${current_output}${current_error}")
endif ()

set(fixture_root
        "${ASTIA_ARCH_INCLUDE_BOUNDARIES_BINARY_DIR}/textual-fixtures")
file(REMOVE_RECURSE "${fixture_root}")

function(assert_fixture_rejected name expected_text)
    execute_process(
            COMMAND "${ASTIA_PYTHON}" "${ASTIA_ARCH_INCLUDE_BOUNDARIES_SCRIPT}"
                    --root "${fixture_root}/${name}"
            RESULT_VARIABLE fixture_result
            OUTPUT_VARIABLE fixture_output
            ERROR_VARIABLE fixture_error)
    if (fixture_result EQUAL 0)
        message(FATAL_ERROR
                "${name} fixture was accepted:\n"
                "${fixture_output}${fixture_error}")
    endif ()
    if (NOT "${fixture_output}${fixture_error}" MATCHES
            "${expected_text}")
        message(FATAL_ERROR
                "${name} fixture failed without the expected diagnostic "
                "'${expected_text}':\n${fixture_output}${fixture_error}")
    endif ()
endfunction()

function(assert_fixture_accepted name)
    execute_process(
            COMMAND "${ASTIA_PYTHON}" "${ASTIA_ARCH_INCLUDE_BOUNDARIES_SCRIPT}"
                    --root "${fixture_root}/${name}"
            RESULT_VARIABLE fixture_result
            OUTPUT_VARIABLE fixture_output
            ERROR_VARIABLE fixture_error)
    if (NOT fixture_result EQUAL 0)
        message(FATAL_ERROR
                "${name} fixture was rejected:\n"
                "${fixture_output}${fixture_error}")
    endif ()
endfunction()

file(MAKE_DIRECTORY
        "${fixture_root}/generic-private/include/astia/arch/x86_64")
file(WRITE "${fixture_root}/generic-private/include/astia/arch/generic.hpp"
        "#include <astia/arch/x86_64/private.hpp>\n")
file(WRITE "${fixture_root}/generic-private/include/astia/arch/x86_64/private.hpp"
        "#pragma once\n")
assert_fixture_rejected("generic-private" "port-private")

file(MAKE_DIRECTORY
        "${fixture_root}/generic-overlay/include/astia/arch")
file(WRITE "${fixture_root}/generic-overlay/include/astia/arch/generic.hpp"
        "#include <include/arch/aarch64/astia/arch/private.hpp>\n")
assert_fixture_rejected("generic-overlay" "port-private")

file(MAKE_DIRECTORY
        "${fixture_root}/aarch64-includes-x86/include/arch/aarch64/astia/arch")
file(WRITE
        "${fixture_root}/aarch64-includes-x86/include/arch/aarch64/astia/arch/port.hpp"
        "#include <astia/arch/x86_64/private.hpp>\n")
assert_fixture_rejected("aarch64-includes-x86" "other architecture")

file(MAKE_DIRECTORY
        "${fixture_root}/x86-includes-aarch64/include/arch/x86_64/astia/arch")
file(WRITE
        "${fixture_root}/x86-includes-aarch64/include/arch/x86_64/astia/arch/port.hpp"
        "#include <astia/arch/aarch64/private.hpp>\n")
assert_fixture_rejected("x86-includes-aarch64" "other architecture")

file(MAKE_DIRECTORY "${fixture_root}/self-include/include/astia/arch")
file(WRITE "${fixture_root}/self-include/include/astia/arch/self.hpp"
        "#include <astia/arch/self.hpp>\n")
assert_fixture_rejected("self-include" "self-include")

file(MAKE_DIRECTORY "${fixture_root}/duplicate-include/include/astia/arch")
file(WRITE "${fixture_root}/duplicate-include/include/astia/arch/dup.hpp"
        "#include <astia/arch/dep.hpp>\n#include <astia/arch/dep.hpp>\n")
file(WRITE "${fixture_root}/duplicate-include/include/astia/arch/dep.hpp"
        "#pragma once\n")
assert_fixture_rejected("duplicate-include" "duplicate include")

file(MAKE_DIRECTORY "${fixture_root}/path-escape/include/astia/arch")
file(WRITE "${fixture_root}/path-escape/include/astia/arch/escape.hpp"
        "#include \"../../../../outside.hpp\"\n")
assert_fixture_rejected("path-escape" "escapes include root")

set(allowed_root "${fixture_root}/allowed")
file(MAKE_DIRECTORY
        "${allowed_root}/include/astia/arch/x86_64"
        "${allowed_root}/include/astia/arch/aarch64"
        "${allowed_root}/include/arch/x86_64/astia/arch"
        "${allowed_root}/include/arch/aarch64/astia/arch"
        "${allowed_root}/src/arch/x86_64"
        "${allowed_root}/src/test")
file(WRITE "${allowed_root}/include/astia/common.hpp" "#pragma once\n")
file(WRITE "${allowed_root}/include/astia/arch/cpu.hpp" "#pragma once\n")
file(WRITE "${allowed_root}/include/astia/arch/x86_64/helper.hpp"
        "#pragma once\n")
file(WRITE "${allowed_root}/include/astia/arch/aarch64/helper.hpp"
        "#pragma once\n")
file(WRITE "${allowed_root}/include/astia/arch/x86_64/user_layout.hpp"
        "#pragma once\n")
file(WRITE "${allowed_root}/include/astia/arch/aarch64/user_layout.hpp"
        "#pragma once\n")
file(WRITE "${allowed_root}/include/astia/arch/generic.hpp"
        "#include <astia/common.hpp>\n")
file(WRITE "${allowed_root}/include/astia/arch/x86_64/private.hpp"
        "#include <astia/arch/cpu.hpp>\n#include <astia/arch/x86_64/helper.hpp>\n")
file(WRITE "${allowed_root}/include/astia/arch/aarch64/private.hpp"
        "#include <astia/arch/cpu.hpp>\n#include <astia/arch/aarch64/helper.hpp>\n")
file(WRITE "${allowed_root}/include/astia/arch/user_layout.hpp"
        "#include <astia/arch/x86_64/user_layout.hpp>\n"
        "#include <astia/arch/aarch64/user_layout.hpp>\n")
file(WRITE "${allowed_root}/include/arch/x86_64/astia/arch/port.hpp"
        "#include <astia/arch/cpu.hpp>\n"
        "#include <astia/arch/x86_64/helper.hpp>\n")
file(WRITE "${allowed_root}/include/arch/aarch64/astia/arch/port.hpp"
        "#include <astia/arch/cpu.hpp>\n"
        "#include <astia/arch/aarch64/helper.hpp>\n")
file(WRITE "${allowed_root}/src/arch/x86_64/port.cpp"
        "#include <astia/arch/x86_64/helper.hpp>\n")
file(WRITE "${allowed_root}/src/test/selftest_aarch64.cpp"
        "#include \"../arch/aarch64/helper.hpp\"\n")
assert_fixture_accepted("allowed")

set(overlay_root
        "${ASTIA_ARCH_INCLUDE_BOUNDARIES_BINARY_DIR}/overlay-resolution")
file(REMOVE_RECURSE "${overlay_root}")
file(MAKE_DIRECTORY
        "${overlay_root}/generic/astia/arch"
        "${overlay_root}/overlay/x86_64/astia/arch")
file(WRITE "${overlay_root}/generic/astia/arch/probe.hpp"
        "#pragma once\n"
        "namespace probe_header { inline constexpr int source = 1; }\n")
file(WRITE "${overlay_root}/overlay/x86_64/astia/arch/probe.hpp"
        "#pragma once\n"
        "namespace probe_header { inline constexpr int source = 2; }\n")
file(WRITE "${overlay_root}/generic/astia/arch/generic_only.hpp"
        "#pragma once\n"
        "namespace probe_header { inline constexpr int only = 3; }\n")
file(WRITE "${overlay_root}/overlay_expected.cpp" [=[
#include <astia/arch/probe.hpp>
#include <astia/arch/generic_only.hpp>

static_assert(probe_header::source == 2,
              "overlay must shadow the generic header");
static_assert(probe_header::only == 3,
              "generic-only header must remain reachable");

int main() {
    return 0;
}
]=])
execute_process(
        COMMAND "${ASTIA_CXX_COMPILER}" -std=c++20 -fsyntax-only
                -I${overlay_root}/overlay/x86_64
                -I${overlay_root}/generic
                "${overlay_root}/overlay_expected.cpp"
        RESULT_VARIABLE overlay_result
        OUTPUT_VARIABLE overlay_output
        ERROR_VARIABLE overlay_error)
if (overlay_result)
    message(FATAL_ERROR
            "overlay resolution fixture failed:\n"
            "${overlay_output}${overlay_error}")
endif ()
execute_process(
        COMMAND "${ASTIA_CXX_COMPILER}" -std=c++20 -fsyntax-only
                -I${overlay_root}/generic
                -I${overlay_root}/overlay/x86_64
                "${overlay_root}/overlay_expected.cpp"
        RESULT_VARIABLE reversed_overlay_result
        OUTPUT_VARIABLE reversed_overlay_output
        ERROR_VARIABLE reversed_overlay_error)
if (reversed_overlay_result EQUAL 0)
    message(FATAL_ERROR
            "overlay resolution check did not detect a generic-first include "
            "order:\n${reversed_overlay_output}${reversed_overlay_error}")
endif ()

set(missing_root
        "${ASTIA_ARCH_INCLUDE_BOUNDARIES_BINARY_DIR}/missing-standalone-dependency")
file(REMOVE_RECURSE "${missing_root}")
file(MAKE_DIRECTORY
        "${missing_root}/include/astia/arch")
file(WRITE "${missing_root}/include/astia/common.hpp"
        "#pragma once\n"
        "namespace astia { using u32 = unsigned int; }\n")
file(WRITE "${missing_root}/include/astia/arch/transitive.hpp" [=[
#pragma once

#include <astia/common.hpp>

template <typename T>
concept transitive = requires(T value) {
    { value } -> std::same_as<T>;
};
]=])
file(WRITE "${missing_root}/transitive.cpp"
        "#include <astia/arch/transitive.hpp>\nint main() { return 0; }\n")
execute_process(
        COMMAND "${ASTIA_CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror
                -pedantic -fsyntax-only -I${missing_root}/include
                "${missing_root}/transitive.cpp"
        RESULT_VARIABLE missing_result
        OUTPUT_VARIABLE missing_output
        ERROR_VARIABLE missing_error)
if (missing_result EQUAL 0)
    message(FATAL_ERROR
            "transitive dependency fixture compiled standalone:\n"
            "${missing_output}${missing_error}")
endif ()
if (NOT "${missing_output}${missing_error}" MATCHES "same_as")
    message(FATAL_ERROR
            "transitive dependency fixture failed without the expected "
            "diagnostic:\n${missing_output}${missing_error}")
endif ()
