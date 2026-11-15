if(NOT EXISTS "${ASTIA_MODULE_ABI_LAYOUT}")
    message(FATAL_ERROR "missing ABI layout host test: ${ASTIA_MODULE_ABI_LAYOUT}")
endif()
if(NOT EXISTS "${ASTIA_VIRTIO_BLK_UNIT}")
    message(FATAL_ERROR "missing virtio-blk host test: ${ASTIA_VIRTIO_BLK_UNIT}")
endif()
if(NOT EXISTS "${ASTIA_USB_UNIT}")
    message(FATAL_ERROR "missing USB host test: ${ASTIA_USB_UNIT}")
endif()

execute_process(
        COMMAND "${ASTIA_MODULE_ABI_LAYOUT}"
        RESULT_VARIABLE _astia_abi_result)
if(NOT _astia_abi_result EQUAL 0)
    message(FATAL_ERROR "ABI layout host test failed: ${_astia_abi_result}")
endif()

execute_process(
        COMMAND "${ASTIA_VIRTIO_BLK_UNIT}"
        RESULT_VARIABLE _astia_virtio_result)
if(NOT _astia_virtio_result EQUAL 0)
    message(FATAL_ERROR "virtio-blk host test failed: ${_astia_virtio_result}")
endif()

execute_process(
        COMMAND "${ASTIA_USB_UNIT}"
        RESULT_VARIABLE _astia_usb_result)
if(NOT _astia_usb_result EQUAL 0)
    message(FATAL_ERROR "USB host test failed: ${_astia_usb_result}")
endif()
