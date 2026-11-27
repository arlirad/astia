#pragma once

#include <astia/common.hpp>

namespace astia::acpi::detail {
    constexpr usize max_aml_nodes           = 2048;
    constexpr usize max_aml_values          = 4096;
    constexpr usize max_aml_name_refs       = 1024;
    constexpr usize max_aml_name_segments   = 16;
    constexpr usize max_aml_package_items   = 256;
    constexpr u32   max_aml_eval_steps      = 100000;
    constexpr u32   max_aml_call_depth      = 8;
    constexpr u32   max_aml_reference_depth = 16;
    constexpr u32   max_aml_package_depth   = 32;
    constexpr u32   max_aml_term_depth      = 64;

    using value_id = u32;
    using node_id  = u32;

    inline constexpr value_id no_value = 0xFFFFFFFFu;
    inline constexpr node_id  no_node  = 0xFFFFFFFFu;

    enum class aml_error : u8 {
        none,
        truncated,
        package_length,
        name,
        unsupported_opcode,
        unsupported_object,
        object_limit,
        value_limit,
        reference_limit,
        recursion_limit,
        execution_limit,
        missing_object,
        wrong_type,
        invalid_value,
        method_arguments,
    };

    const char* aml_error_text(aml_error error);

    enum class value_kind : u8 {
        none,
        integer,
        string,
        buffer,
        package,
        name_reference,
    };

    enum class node_kind : u8 {
        root,
        scope,
        device,
        method,
        name,
        field,
        mutex,
        event,
        region,
        processor,
        power_resource,
        thermal_zone,
        alias,
        external,
    };

    struct value {
        const u8* data      = nullptr;
        u64       integer   = 0;
        u32       length    = 0;
        u32       first     = no_value;
        u32       last      = no_value;
        u32       next      = no_value;
        u32       reference = no_value;
        u16       count     = 0;
        u8        kind      = static_cast<u8>(value_kind::none);
        u8        reserved  = 0;
    };

    struct name_ref {
        u32  segment[max_aml_name_segments]{};
        u8   count         = 0;
        u8   parent_prefix = 0;
        bool root          = false;
    };

    struct node {
        u32       name         = 0;
        u32       parent       = no_node;
        u32       first_child  = no_node;
        u32       next_sibling = no_node;
        u32       value        = no_value;
        u32       target       = no_node;
        const u8* body         = nullptr;
        u32       body_length  = 0;
        u8        kind         = static_cast<u8>(node_kind::scope);
        u8        arg_count    = 0;
        u8        flags        = 0;
        u8        object_type  = 0;
    };
} // namespace astia::acpi::detail
