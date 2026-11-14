set(ASTIA_VIRTIO_BLK_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/virtio_blk.o)

add_custom_command(
        OUTPUT ${ASTIA_VIRTIO_BLK_OBJECT}
        COMMAND ${CMAKE_CXX_COMPILER}
                -std=c++20 -ffreestanding -fno-builtin -fno-pic -fno-pie
                -fno-exceptions -fno-rtti -fno-threadsafe-statics
                -fno-stack-protector -fno-omit-frame-pointer -mno-red-zone
                -mcmodel=large -fno-asynchronous-unwind-tables -Wall -Wextra -pedantic
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -c ${CMAKE_CURRENT_SOURCE_DIR}/virtio_blk.cpp -o ${ASTIA_VIRTIO_BLK_OBJECT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/virtio_blk.cpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi.h
        COMMENT "Compiling virtio-blk loadable module"
        VERBATIM
)

add_custom_target(virtio_blk_module ALL DEPENDS ${ASTIA_VIRTIO_BLK_OBJECT})
