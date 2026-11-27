#pragma once

#include "../tables.hpp"
#include "opcode.hpp"
#include "types.hpp"

namespace astia::acpi::detail {
    inline constexpr u64 interpreter_revision = 0x20230628ull;

    class interpreter {
        public:
        bool build(const table_set& tables);

        bool evaluate_s5(value_id& package);

        aml_error error() const {
            return _error;
        }

        const value* get(value_id id) const {
            return id < _value_count ? &_values[id] : nullptr;
        }

        bool integer(value_id id, u64& out) {
            return as_integer(id, out);
        }

        usize node_count() const {
            return _node_count;
        }

        usize value_count() const {
            return _value_count;
        }

        private:
        struct cursor {
            const u8* position = nullptr;
            const u8* end      = nullptr;
        };

        struct execution {
            value_id local[8]{};
            value_id argument[7]{};
            node_id  scope       = 0;
            value_id returned    = no_value;
            bool     has_return  = false;
            bool     do_break    = false;
            bool     do_continue = false;
        };

        struct target {
            value_id* slot    = nullptr;
            bool      discard = false;
        };

        bool fail(aml_error error);
        bool at_end(const cursor& c) const;
        bool enter_term();
        void leave_term();

        struct term_guard {
            interpreter* owner  = nullptr;
            bool         active = false;

            explicit term_guard(interpreter& value)
                : owner(&value), active(value.enter_term()) {}

            ~term_guard() {
                if (active)
                    owner->leave_term();
            }

            explicit operator bool() const {
                return active;
            }
        };

        bool read_u8(cursor& c, u8& value);
        bool read_u16(cursor& c, u16& value);
        bool read_u32(cursor& c, u32& value);
        bool read_u64(cursor& c, u64& value);

        bool      read_package_length(cursor& c, u32& value, u32& bytes);
        const u8* open_package(cursor& c);
        bool      close_package(cursor& c, const u8* end);

        bool read_name_segment(cursor& c, u32& segment);
        bool read_name_ref(cursor& c, name_ref& reference);

        node_id add_node(node_id parent, u32 name, node_kind kind);
        node_id find_child(node_id parent, u32 name) const;
        node_id resolve_lookup(const name_ref& reference, node_id scope) const;
        node_id resolve_definition(const name_ref& reference, node_id scope,
                                   bool create_parents);

        node_id root() const {
            return 0;
        }

        node_id follow_alias(node_id id, u32 depth);

        value_id add_value(value_kind kind);
        value_id add_integer(u64 value);
        value_id add_name_reference(const name_ref& reference);
        bool     append_package_item(value_id package, value_id item);

        value_id dereference(value_id id);
        value_id dereference(value_id id, u32 depth);
        bool     as_integer(value_id id, u64& out);
        bool     as_package(value_id id, const value*& out);
        value_id make_package_item(value_id package, value_id item);

        bool parse_scope(const table_set& tables);
        bool parse_term_list(cursor& c, node_id scope);
        bool parse_definition(cursor& c, node_id scope);

        bool parse_name_object(cursor& c, node_id scope);
        bool parse_scope_object(cursor& c, node_id scope, node_kind kind,
                                u8 arg_count = 0, u32 object_type = 0);
        bool parse_method(cursor& c, node_id scope);
        bool parse_field(cursor& c, node_id scope);
        bool parse_index_field(cursor& c, node_id scope);
        bool parse_bank_field(cursor& c, node_id scope);
        bool parse_field_list(cursor& c, const u8* end, node_id scope);
        bool parse_operation_region(cursor& c, node_id scope);
        bool parse_alias(cursor& c, node_id scope);
        bool parse_external(cursor& c, node_id scope);
        bool parse_mutex(cursor& c, node_id scope);
        bool parse_event(cursor& c, node_id scope);
        bool parse_data_table_region(cursor& c, node_id scope);

        bool parse_data_object(cursor& c, value_id& out);
        bool parse_package(cursor& c, value_id& out, bool variable);
        bool parse_buffer(cursor& c, value_id& out);

        bool eval_constant(cursor& c, node_id scope, u64& out);
        bool eval_term(cursor& c, execution& e, value_id& out);
        bool eval_name(cursor& c, execution& e, value_id& out);
        bool eval_package(cursor& c, execution& e, value_id& out, bool variable);
        bool eval_buffer(cursor& c, execution& e, value_id& out);
        bool eval_target(cursor& c, execution& e, target& out);
        bool eval_target_for_node(node_id id, target& out);
        bool execute_statement(cursor& c, execution& e);
        bool execute_if(cursor& c, execution& e);
        bool execute_while(cursor& c, execution& e);
        bool execute_method(node_id method, const value_id* arguments, usize count,
                            value_id& out);

        bool step();

        node      _nodes[max_aml_nodes]{};
        usize     _node_count = 0;
        value     _values[max_aml_values]{};
        usize     _value_count = 0;
        name_ref  _references[max_aml_name_refs]{};
        usize     _reference_count = 0;
        aml_error _error           = aml_error::none;
        u32       _steps           = 0;
        u32       _call_depth      = 0;
        u32       _term_depth      = 0;
        const u8* _package_ends[max_aml_package_depth]{};
        u32       _package_depth = 0;
    };
} // namespace astia::acpi::detail
