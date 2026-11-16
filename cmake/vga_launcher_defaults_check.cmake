set(root "${CMAKE_CURRENT_LIST_DIR}/..")

file(READ "${root}/CMakeLists.txt" cmake_lists)
file(READ "${root}/toolchain/run-zsh-vga-qemu.sh" launcher)
file(READ "${root}/toolchain/zsh-vga-harness.py" harness)

string(FIND "${cmake_lists}" "set(ASTIA_VGA_CPUS \"2\"" cmake_cpus)
string(FIND "${cmake_lists}" "set(ASTIA_VGA_MEMORY \"256M\"" cmake_memory)
string(FIND "${launcher}" "cpus=\${ASTIA_VGA_CPUS:-2}" launcher_cpus)
string(FIND "${launcher}" "memory=\${ASTIA_VGA_MEMORY:-256M}" launcher_memory)
string(FIND "${harness}" "parser.add_argument(\"--cpus\", type=int, default=2)" harness_cpus)
string(FIND "${harness}" "parser.add_argument(\"--memory\", default=\"256M\")" harness_memory)

foreach(result IN ITEMS cmake_cpus cmake_memory launcher_cpus launcher_memory harness_cpus harness_memory)
    if(${result} EQUAL -1)
        message(FATAL_ERROR "missing VGA default: ${result}")
    endif()
endforeach()
