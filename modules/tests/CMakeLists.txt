add_library(astia_module_fixtures INTERFACE)

target_include_directories(astia_module_fixtures INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../kernel/include)
