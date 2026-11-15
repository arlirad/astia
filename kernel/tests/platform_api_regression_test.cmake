if (NOT DEFINED ASTIA_KERNEL_SOURCE_DIR OR NOT DEFINED ASTIA_CXX_COMPILER)
    message(FATAL_ERROR "ASTIA_KERNEL_SOURCE_DIR and ASTIA_CXX_COMPILER are required")
endif ()

set(test_root "${ASTIA_PLATFORM_API_TEST_BINARY_DIR}/platform-api-regression")
file(REMOVE_RECURSE "${test_root}")
file(MAKE_DIRECTORY "${test_root}")
set(source "${test_root}/removed_discovered_cpu_count.cpp")
file(WRITE "${source}" [=[
#include <astia/arch/platform.hpp>

int main() {
    return static_cast<int>(astia::arch::platform::discovered_cpu_count());
}
]=])

execute_process(
        COMMAND "${ASTIA_CXX_COMPILER}" -std=c++20 -fsyntax-only
                -I "${ASTIA_KERNEL_SOURCE_DIR}/include" "${source}"
        RESULT_VARIABLE compile_result
        OUTPUT_VARIABLE compile_output
        ERROR_VARIABLE compile_error)
if (compile_result EQUAL 0)
    message(FATAL_ERROR
            "removed discovered_cpu_count API unexpectedly compiled:\n"
            "${compile_output}\n${compile_error}")
endif ()
