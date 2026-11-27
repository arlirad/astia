#include "interpreter.hpp"

namespace astia::acpi::detail {
    const char* aml_error_text(aml_error error) {
        switch (error) {
        case aml_error::none:
            return "ok";
        case aml_error::truncated:
            return "truncated AML";
        case aml_error::package_length:
            return "AML package length";
        case aml_error::name:
            return "AML name";
        case aml_error::unsupported_opcode:
            return "unsupported AML opcode";
        case aml_error::unsupported_object:
            return "unsupported AML object";
        case aml_error::object_limit:
            return "AML object limit";
        case aml_error::value_limit:
            return "AML value limit";
        case aml_error::reference_limit:
            return "AML reference limit";
        case aml_error::recursion_limit:
            return "AML recursion limit";
        case aml_error::execution_limit:
            return "AML execution limit";
        case aml_error::missing_object:
            return "AML missing object";
        case aml_error::wrong_type:
            return "AML wrong type";
        case aml_error::invalid_value:
            return "AML invalid value";
        case aml_error::method_arguments:
            return "AML method arguments";
        }

        return "unknown";
    }

    bool interpreter::fail(aml_error error) {
        if (_error == aml_error::none)
            _error = error;

        return false;
    }

    bool interpreter::at_end(const cursor& c) const {
        return c.position >= c.end;
    }

    bool interpreter::enter_term() {
        if (_term_depth == max_aml_term_depth)
            return fail(aml_error::recursion_limit);

        ++_term_depth;

        return true;
    }

    void interpreter::leave_term() {
        if (_term_depth != 0)
            --_term_depth;
    }

    bool interpreter::step() {
        if (_steps == max_aml_eval_steps)
            return fail(aml_error::execution_limit);

        ++_steps;

        return true;
    }
} // namespace astia::acpi::detail
