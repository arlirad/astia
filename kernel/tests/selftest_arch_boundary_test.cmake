if (NOT DEFINED ASTIA_KERNEL_SOURCE_DIR)
    message(FATAL_ERROR "ASTIA_KERNEL_SOURCE_DIR is required")
endif ()

file(READ "${ASTIA_KERNEL_SOURCE_DIR}/src/test/selftest.cpp" generic_source)
foreach (forbidden
        "arch/x86_64"
        "x86_64::"
        "interrupt_dispatch"
        "int $3")
    string(FIND "${generic_source}" "${forbidden}" position)
    if (NOT position EQUAL -1)
        message(FATAL_ERROR
                "generic selftest contains architecture-specific text: ${forbidden}")
    endif ()
endforeach ()

string(FIND "${generic_source}" "selftest_arch.hpp" arch_api_include)
if (arch_api_include EQUAL -1)
    message(FATAL_ERROR "generic selftest does not use the architecture test API")
endif ()

file(READ "${ASTIA_KERNEL_SOURCE_DIR}/src/test/selftest_x86_64.cpp" x86_source)
string(FIND "${x86_source}" "../arch/x86_64/smp.hpp" x86_include)
if (x86_include EQUAL -1)
    message(FATAL_ERROR "x86 selftest does not own its architecture-specific include")
endif ()

if (NOT EXISTS "${ASTIA_KERNEL_SOURCE_DIR}/src/test/selftest_aarch64.cpp")
    message(FATAL_ERROR "AArch64 selftest policy source is missing")
endif ()
