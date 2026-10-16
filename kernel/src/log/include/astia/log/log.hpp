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
        auto put(level severity, char c) -> void;
        auto prefix(level severity) -> void;
        auto number(level severity, u64 value, unsigned base, bool upper) -> void;
        auto signed_number(level severity, i64 value) -> void;

        inline auto write_arg(level severity, char spec, const char* value) -> void {
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

        inline auto write_arg(level severity, char spec, char value) -> void {
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
        auto write_arg(level severity, char spec, value_type value) -> void {
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

        inline auto write_format(level severity, const char* format) -> void {
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
        auto write_format(level severity, const char* format, value_type&& value,
                          rest_types&&... rest) -> void {
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
        auto print(level severity, const char* format, arg_types&&... args) -> void {
            prefix(severity);
            write_format(severity, format, static_cast<arg_types&&>(args)...);
            put(severity, '\n');
        }
    } // namespace detail

    template <typename... arg_types>
    auto print(level severity, const char* format, arg_types&&... args) -> void {
        detail::print(severity, format, static_cast<arg_types&&>(args)...);
    }

    template <typename... arg_types>
    auto debug(const char* format, arg_types&&... args) -> void {
        detail::print(level::debug, format, static_cast<arg_types&&>(args)...);
    }

    template <typename... arg_types>
    auto info(const char* format, arg_types&&... args) -> void {
        detail::print(level::info, format, static_cast<arg_types&&>(args)...);
    }

    template <typename... arg_types>
    auto warning(const char* format, arg_types&&... args) -> void {
        detail::print(level::warning, format, static_cast<arg_types&&>(args)...);
    }

    template <typename... arg_types>
    auto error(const char* format, arg_types&&... args) -> void {
        detail::print(level::error, format, static_cast<arg_types&&>(args)...);
    }

} // namespace astia::log
