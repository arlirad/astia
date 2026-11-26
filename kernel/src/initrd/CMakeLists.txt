set(_astia_third_party ${ASTIA_KERNEL_SOURCE_DIR}/../third_party)

# Vendored, allocation-free decoder subset.  The upstream sources are kept
# outside the kernel tree and excluded from Astia style/format rules; this
# target supplies only the freestanding headers they expect.
add_library(initrd_decoders STATIC
        ${_astia_third_party}/zstd/zstddeclib.c
        ${_astia_third_party}/miniz/miniz_tinfl.c
)

target_include_directories(initrd_decoders
        PUBLIC ${_astia_third_party}/zstd ${_astia_third_party}/miniz
        PRIVATE ${_astia_third_party}/compat
)

target_compile_options(initrd_decoders PRIVATE -w)
target_compile_definitions(initrd_decoders PRIVATE
        MINIZ_NO_MALLOC
        MINIZ_NO_STDIO
        MINIZ_NO_TIME
        MINIZ_NO_ARCHIVE_APIS
        XXH_NO_STDLIB
)

add_library(initrd STATIC
        initrd.cpp
        decompress.cpp
        boot.cpp
)

target_include_directories(initrd PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
)

target_link_libraries(initrd PUBLIC libcore mm PRIVATE initrd_decoders)
