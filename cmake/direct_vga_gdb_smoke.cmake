set(root "${CMAKE_CURRENT_LIST_DIR}/..")
set(kernel "${CMAKE_CURRENT_BINARY_DIR}/direct-vga-gdb-smoke/astiakrn")
set(initrd "${CMAKE_CURRENT_BINARY_DIR}/direct-vga-gdb-smoke/initrd.cpio")

execute_process(
    COMMAND ${CMAKE_COMMAND} -E env
            ASTIA_DIRECT_SKIP_BUILD=1
            ASTIA_DIRECT_KERNEL=${kernel}
            ASTIA_DIRECT_INITRD=${initrd}
            ASTIA_DIRECT_DISPLAY=none
            QEMU=qemu-test
            sh ${root}/kernel/scripts/run-direct-vga-gdb.sh --dry-run
    RESULT_VARIABLE wait_result
    OUTPUT_VARIABLE wait_output
    ERROR_VARIABLE wait_error)
if(NOT wait_result EQUAL 0)
    message(FATAL_ERROR "direct VGA/GDB wait smoke failed: ${wait_error}")
endif()

foreach(needle IN ITEMS
        "-kernel ${kernel}"
        "-initrd ${initrd}"
        "-smp 2"
        "-m 256M"
        "-vga std"
        "-display none"
        "-S"
        "-gdb tcp::1234"
        "boot marker: boot ok"
        "framebuffer descriptor: unavailable (PVH); serial fallback")
    string(FIND "${wait_output}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "direct VGA/GDB wait smoke missing '${needle}': ${wait_output}")
    endif()
endforeach()

execute_process(
    COMMAND ${CMAKE_COMMAND} -E env
            ASTIA_DIRECT_SKIP_BUILD=1
            ASTIA_DIRECT_KERNEL=${kernel}
            ASTIA_DIRECT_INITRD=${initrd}
            ASTIA_DIRECT_DISPLAY=none
            QEMU=qemu-test
            sh ${root}/kernel/scripts/run-direct-vga-gdb.sh --attach --dry-run
    RESULT_VARIABLE attach_result
    OUTPUT_VARIABLE attach_output
    ERROR_VARIABLE attach_error)
if(NOT attach_result EQUAL 0)
    message(FATAL_ERROR "direct VGA/GDB attach smoke failed: ${attach_error}")
endif()
string(FIND "${attach_output}" "-gdb tcp::1234" attach_gdb)
string(FIND "${attach_output}" "-S" attach_stop)
if(attach_gdb EQUAL -1 OR NOT attach_stop EQUAL -1)
    message(FATAL_ERROR "direct VGA/GDB attach mode is not non-stop: ${attach_output}")
endif()
