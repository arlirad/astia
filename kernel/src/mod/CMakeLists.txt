file(GLOB_RECURSE ASTIA_MOD_SYSCALL_SOURCES CONFIGURE_DEPENDS syscalls/*.cpp)

find_program(ASTIA_MOD_XXD xxd REQUIRED)

set(ASTIA_DEVFS_MODULE_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/devfs_resident.o)
set(ASTIA_DEVFS_MODULE_BLOB ${CMAKE_CURRENT_BINARY_DIR}/devfs_resident_blob.cpp)

add_custom_command(
        OUTPUT ${ASTIA_DEVFS_MODULE_OBJECT}
        COMMAND ${CMAKE_CXX_COMPILER}
                -std=c++20 -ffreestanding -fno-builtin -fno-pic -fno-pie
                -fno-exceptions -fno-rtti -fno-threadsafe-statics
                -fno-stack-protector -fno-omit-frame-pointer -mno-red-zone
                -mcmodel=large -fno-asynchronous-unwind-tables -fno-use-cxa-atexit
                -Wall -Wextra -Werror -pedantic
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -I${ASTIA_KERNEL_SOURCE_DIR}/include/arch/${ARCH}
                -c ${ASTIA_KERNEL_SOURCE_DIR}/../modules/devfs/devfs.cpp
                -o ${ASTIA_DEVFS_MODULE_OBJECT}
        DEPENDS
                ${ASTIA_KERNEL_SOURCE_DIR}/../modules/devfs/devfs.cpp
                ${ASTIA_KERNEL_SOURCE_DIR}/../modules/devfs/devfs.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/fs/device_node.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/fs/filesystem.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/fs/mount.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/dev/pty.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/proc/terminal.hpp
        COMMENT "Compiling resident devfs boot module"
        VERBATIM)

add_custom_command(
        OUTPUT ${ASTIA_DEVFS_MODULE_BLOB}
        COMMAND ${CMAKE_COMMAND} -E env XXD=${ASTIA_MOD_XXD}
                sh ${ASTIA_KERNEL_SOURCE_DIR}/tests/embed_blob.sh
                ${ASTIA_DEVFS_MODULE_OBJECT} astia_devfs_module_image
                ${ASTIA_DEVFS_MODULE_BLOB}
        DEPENDS ${ASTIA_DEVFS_MODULE_OBJECT}
                ${ASTIA_KERNEL_SOURCE_DIR}/tests/embed_blob.sh
        COMMENT "Embedding resident devfs boot module"
        VERBATIM)

add_library(mod STATIC
        ${ASTIA_MOD_SYSCALL_SOURCES}
        ${ASTIA_DEVFS_MODULE_BLOB}
        exports.cpp
        module_driver.cpp
        module.cpp
        module_image.cpp
        irq_route.cpp
        init.cpp
)

target_include_directories(mod PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
        ${ASTIA_KERNEL_SOURCE_DIR}/src/log
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys
)

target_link_libraries(mod PUBLIC elf initrd libcore mm dev net log proc fs io sys)
