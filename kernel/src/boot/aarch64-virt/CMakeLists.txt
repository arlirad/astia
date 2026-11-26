add_executable(astiakrn
        exception.cpp
        fdt.cpp
        kmain.cpp
        scheduler.cpp
)

target_include_directories(astiakrn PRIVATE
        ${ASTIA_KERNEL_SOURCE_DIR}/include
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch/aarch64
)

target_link_libraries(astiakrn PRIVATE arch_aarch64 libcore dbg plat_aarch64_virt)

target_compile_options(astiakrn PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-ffunction-sections>
        $<$<COMPILE_LANGUAGE:CXX>:-fdata-sections>
)

target_link_options(astiakrn PRIVATE
        "-T${ASTIA_LINKER_SCRIPT}"
        "-Wl,--gc-sections"
        "-Wl,-z,max-page-size=4096"
)

set_target_properties(astiakrn PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src"
)
