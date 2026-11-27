set(ASTIA_PUBLIC_HEADERS
        astia/api/syscalls.h
        astia/acpi.hpp
        astia/arch/io.hpp
        astia/shutdown.hpp
        astia/arch/user_layout.hpp
        astia/boot/framebuffer.hpp
        astia/boot/virt/multiboot2.hpp
        astia/boot/virt/pvh.hpp
        astia/core/completion.hpp
        astia/core/endian.hpp
        astia/core/mpmc_queue.hpp
        astia/core/rcu_domain.hpp
        astia/core/rcu_hashmap.hpp
        astia/core/rcu_runtime.hpp
        astia/rcu/vector.hpp
        astia/core/resume_queue.hpp
        astia/core/spinlock.hpp
        astia/core/task.hpp
        astia/dev/blk/blkdev.hpp
        astia/dev/capabilities.hpp
        astia/dev/chr/chrdev.hpp
        astia/dev/device.hpp
        astia/dev/device_file.hpp
        astia/dev/driver.hpp
        astia/dev/io.hpp
        astia/dev/irq.hpp
        astia/dev/mmio.hpp
        astia/dev/pci.hpp
        astia/dev/regs.hpp
        astia/dev/tty_input.hpp
        astia/dev/uart_platform.hpp
        astia/fs.hpp
        astia/hw/driver.hpp
        astia/hw/mmio.hpp
        astia/hw/pci.hpp
        astia/io.hpp
        astia/mm/iovec.hpp
        astia/mm/dma_buffer.hpp
        astia/mm/memory_map.hpp
        astia/net.hpp
        astia/proc/user_layout.hpp
        astia/tuple.hpp)

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
