#pragma once

#include <astia/common.hpp>
#include <type_traits>

namespace astia::log {

    enum class level {
        debug,
        info,
        warning,
        error,
        panic,
    };

    namespace detail {
        void put(level severity, char c);
        void prefix(level severity);
        void number(level severity, u64 value, unsigned base, bool upper);
        void signed_number(level severity, i64 value);

        inline void write_arg(level severity, char spec, const char* value) {
            if (spec == 's') {
                if (value == nullptr) {
                    value = "(null)";
                }
                while (*value != '\0') {
                    put(severity, *value++);
                }
                return;
            }
            put(severity, '0');
            put(severity, 'x');
            number(severity, reinterpret_cast<u64>(value), 16, false);
        }

        inline void write_arg(level severity, char spec, char value) {
            if (spec == 'c') {
                put(severity, value);
            }
            else if (spec == 'd' || spec == 'i') {
                signed_number(severity, value);
            }
            else {
                number(severity, static_cast<u64>(static_cast<unsigned char>(value)),
                       spec == 'x' || spec == 'X' ? 16 : 10, spec == 'X');
            }
        }

        template <typename value_type>
        void write_arg(level severity, char spec, value_type value) {
            static_assert(std::is_integral_v<value_type> || std::is_pointer_v<value_type>,
                          "unsupported astia::log format argument");
            if constexpr (std::is_pointer_v<value_type>) {
                put(severity, '0');
                put(severity, 'x');
                number(severity, reinterpret_cast<u64>(value), 16, false);
            }
            else if constexpr (std::is_signed_v<value_type>) {
                if (spec == 'd' || spec == 'i') {
                    signed_number(severity, static_cast<i64>(value));
                }
                else {
                    number(severity, static_cast<u64>(value),
                           spec == 'x' || spec == 'X' ? 16 : 10, spec == 'X');
                }
            }
            else {
                number(severity, static_cast<u64>(value),
                       spec == 'x' || spec == 'X' ? 16 : 10, spec == 'X');
            }
        }

        inline void write_format(level severity, const char* format) {
            while (*format != '\0') {
                if (*format == '%' && format[1] == '%') {
                    put(severity, '%');
                    format += 2;
                }
                else {
                    put(severity, *format++);
                }
            }
        }

        template <typename value_type, typename... rest_types>
        void write_format(level severity, const char* format, value_type&& value,
                          rest_types&&... rest) {
            while (*format != '\0') {
                if (*format != '%') {
                    put(severity, *format++);
                    continue;
                }
                ++format;
                if (*format == '%') {
                    put(severity, '%');
                    ++format;
                    continue;
                }

                while (*format == 'l' || *format == 'z') {
                    ++format;
                }
                if (*format == '\0') {
                    put(severity, '%');
                    return;
                }
                const char spec = *format++;
                write_arg(severity, spec, static_cast<value_type&&>(value));
                write_format(severity, format, static_cast<rest_types&&>(rest)...);
                return;
            }
        }

        template <typename... arg_types>
        void print(level severity, const char* format, arg_types&&... args) {
            prefix(severity);
            write_format(severity, format, static_cast<arg_types&&>(args)...);
            put(severity, '\n');
        }
    } // namespace detail

    template <typename... arg_types>
    void print(level severity, const char* format, arg_types&&... args) {
        detail::print(severity, format, static_cast<arg_types&&>(args)...);
    }

    template <typename... arg_types>
    void debug(const char* format, arg_types&&... args) {
        detail::print(level::debug, format, static_cast<arg_types&&>(args)...);
    }

    template <typename... arg_types>
    void info(const char* format, arg_types&&... args) {
        detail::print(level::info, format, static_cast<arg_types&&>(args)...);
    }

    template <typename... arg_types>
    void warning(const char* format, arg_types&&... args) {
        detail::print(level::warning, format, static_cast<arg_types&&>(args)...);
    }

    template <typename... arg_types>
    void error(const char* format, arg_types&&... args) {
        detail::print(level::error, format, static_cast<arg_types&&>(args)...);
    }

} // namespace astia::log
