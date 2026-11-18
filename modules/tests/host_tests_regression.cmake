if(NOT EXISTS "${ASTIA_TYPED_CONTRACT_LAYOUT}")
    message(FATAL_ERROR "missing typed contract host test: ${ASTIA_TYPED_CONTRACT_LAYOUT}")
endif()
if(NOT EXISTS "${ASTIA_VIRTIO_BLK_UNIT}")
    message(FATAL_ERROR "missing virtio-blk host test: ${ASTIA_VIRTIO_BLK_UNIT}")
endif()
if(NOT EXISTS "${ASTIA_USB_UNIT}")
    message(FATAL_ERROR "missing USB host test: ${ASTIA_USB_UNIT}")
endif()
if(NOT EXISTS "${ASTIA_RTL8139_UNIT}")
    message(FATAL_ERROR "missing RTL8139 host test: ${ASTIA_RTL8139_UNIT}")
endif()

execute_process(
        COMMAND "${ASTIA_TYPED_CONTRACT_LAYOUT}"
        RESULT_VARIABLE _astia_typed_result)
if(NOT _astia_typed_result EQUAL 0)
    message(FATAL_ERROR "typed contract host test failed: ${_astia_typed_result}")
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

execute_process(
        COMMAND "${ASTIA_RTL8139_UNIT}"
        RESULT_VARIABLE _astia_rtl8139_result)
if(NOT _astia_rtl8139_result EQUAL 0)
    message(FATAL_ERROR "RTL8139 host test failed: ${_astia_rtl8139_result}")
endif()
