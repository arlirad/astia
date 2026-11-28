if(NOT DEFINED ARCH)
    set(ARCH "x86_64")
endif()
# Modules are built for one port, exactly like the kernel: the generic include
# root plus the build-selected port overlay.  A module that uses the
# port-owned context classes compiles with both.
set(ASTIA_ARCH_INCLUDE_DIR
        ${ASTIA_KERNEL_SOURCE_DIR}/include/arch/${ARCH})

add_subdirectory(tests)
add_subdirectory(nvme)
add_subdirectory(virtio-blk)
add_subdirectory(usb)
add_subdirectory(rtl8139)
add_subdirectory(ps2-keyboard)
add_subdirectory(ip)
add_subdirectory(udp)
add_subdirectory(tcp)
add_subdirectory(eth)
add_subdirectory(ext4)
