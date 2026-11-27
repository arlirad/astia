set(ASTIA_DEVFS_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/devfs.o)
set(ASTIA_DEVFS_BLOB ${CMAKE_CURRENT_BINARY_DIR}/devfs_module_blob.cpp)
find_program(ASTIA_DEVFS_XXD xxd REQUIRED)

add_custom_command(
        OUTPUT ${ASTIA_DEVFS_OBJECT}
        COMMAND ${CMAKE_CXX_COMPILER}
                -std=c++20 -ffreestanding -fno-builtin -fno-pic -fno-pie
                -fno-exceptions -fno-rtti -fno-threadsafe-statics
                -fno-stack-protector -fno-omit-frame-pointer -mno-red-zone
                -mcmodel=large -fno-asynchronous-unwind-tables -fno-use-cxa-atexit
                -Wall -Wextra -Werror -pedantic
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -I${ASTIA_ARCH_INCLUDE_DIR}
                -c ${CMAKE_CURRENT_SOURCE_DIR}/devfs.cpp -o ${ASTIA_DEVFS_OBJECT}
        DEPENDS
                ${CMAKE_CURRENT_SOURCE_DIR}/devfs.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/devfs.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/fs/device_node.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/fs/filesystem.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/fs/mount.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/dev/pty.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/proc/terminal.hpp
        COMMENT "Compiling devfs resident boot module"
        VERBATIM)
add_custom_target(devfs_module ALL DEPENDS ${ASTIA_DEVFS_OBJECT})

add_custom_command(
        OUTPUT ${ASTIA_DEVFS_BLOB}
        COMMAND ${CMAKE_COMMAND} -E env XXD=${ASTIA_DEVFS_XXD}
                sh ${ASTIA_KERNEL_SOURCE_DIR}/tests/embed_blob.sh
                ${ASTIA_DEVFS_OBJECT} astia_devfs_module_image ${ASTIA_DEVFS_BLOB}
        DEPENDS ${ASTIA_DEVFS_OBJECT}
                ${ASTIA_KERNEL_SOURCE_DIR}/tests/embed_blob.sh
        COMMENT "Embedding devfs resident boot module"
        VERBATIM)
add_custom_target(devfs_embed ALL DEPENDS ${ASTIA_DEVFS_BLOB})
