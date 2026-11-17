set(ASTIA_PUBLIC_HEADERS
        astia/arch/io.hpp
        astia/arch/user_layout.hpp
        astia/boot/framebuffer.hpp
        astia/boot/virt/multiboot2.hpp
        astia/boot/virt/pvh.hpp
        astia/dev/blk/blkdev.hpp
        astia/dev/device.hpp
        astia/dev/chr/chrdev.hpp
        astia/dev/driver.hpp
        astia/dev/io.hpp
        astia/dev/irq.hpp
        astia/dev/mmio.hpp
        astia/dev/pci.hpp
        astia/dev/regs.hpp
        astia/io.hpp
        astia/mm/iovec.hpp
        astia/mm/memory_map.hpp
        astia/module_abi.hpp
        astia/net.hpp
        astia/proc/user_layout.hpp)

file(MAKE_DIRECTORY "${ASTIA_PUBLIC_HEADER_TEST_BINARY_DIR}")
foreach(ASTIA_PUBLIC_HEADER IN LISTS ASTIA_PUBLIC_HEADERS)
    string(REPLACE "/" "_" ASTIA_PUBLIC_HEADER_NAME "${ASTIA_PUBLIC_HEADER}")
    set(ASTIA_OUTPUT "${ASTIA_PUBLIC_HEADER_TEST_BINARY_DIR}/${ASTIA_PUBLIC_HEADER_NAME}.o")
    execute_process(
            COMMAND "${ASTIA_CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror -pedantic
                    -I${ASTIA_KERNEL_SOURCE_DIR}/include
                    -include ${ASTIA_KERNEL_SOURCE_DIR}/include/${ASTIA_PUBLIC_HEADER}
                    -x c++ -c ${ASTIA_PUBLIC_HEADER_TEST_SOURCE} -o ${ASTIA_OUTPUT}
            RESULT_VARIABLE ASTIA_RESULT
            OUTPUT_VARIABLE ASTIA_OUTPUT_TEXT
            ERROR_VARIABLE ASTIA_ERROR_TEXT)
    if(NOT ASTIA_RESULT EQUAL 0)
        message(FATAL_ERROR
                "Public header ${ASTIA_PUBLIC_HEADER} failed standalone compilation:\n"
                "${ASTIA_OUTPUT_TEXT}${ASTIA_ERROR_TEXT}")
    endif()
endforeach()
