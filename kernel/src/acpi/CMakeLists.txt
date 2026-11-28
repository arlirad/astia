if (ARCH STREQUAL "aarch64")
    add_library(acpi STATIC stub.cpp)

    target_include_directories(acpi PUBLIC
            ${ASTIA_KERNEL_SOURCE_DIR}/include
    )

    return()
endif ()

add_library(acpi_madt STATIC
        madt.cpp
)

target_include_directories(acpi_madt PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
)

target_link_libraries(acpi_madt PUBLIC libcore)

add_library(acpi STATIC
        tables.cpp
        aml/core.cpp
        aml/decode.cpp
        aml/eval.cpp
        aml/namespace.cpp
        aml/parse.cpp
        power.cpp
        prt.cpp
        resources.cpp
        init.cpp
)

target_include_directories(acpi PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
)

target_include_directories(acpi PRIVATE
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch/x86_64
)

target_link_libraries(acpi PUBLIC libcore core log hw arch_${ARCH} mm acpi_madt)
