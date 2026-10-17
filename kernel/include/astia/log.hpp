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

    /// A context's own name: a compile-time string, so the name is part of
    /// the type and costs nothing to carry, copy or compare.
    template <u32 N>
    struct name {
        char text[N]{};

        static constexpr u32 length = N - 1;

        constexpr name() = default;

        constexpr name(const char (&value)[N]) {
            for (u32 i = 0; i < N; ++i) {
                text[i] = value[i];
            }
        }
    };

    namespace detail {

        /// A context's whole name: its parent's, a dot, and its own, so
        /// `dev` then `uart` is `dev::uart`.  A context with no parent is
        /// its own full name.
        template <name Own, typename Parent>
        consteval auto join() {
            if constexpr (std::is_void_v<Parent>) {
                return Own;
            }
            else {
                constexpr auto                           parent = Parent::full_name;
                name<parent.length + 2 + Own.length + 1> joined{};
                for (u32 i = 0; i < parent.length; ++i) {
                    joined.text[i] = parent.text[i];
                }
                joined.text[parent.length]     = ':';
                joined.text[parent.length + 1] = ':';
                for (u32 i = 0; i < Own.length; ++i) {
                    joined.text[parent.length + 2 + i] = Own.text[i];
                }
                return joined;
            }
        }

        void put(level severity, char c);
        void prefix(level severity, const char* context, u32 length);
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

        template <u32 N, typename... arg_types>
        void print(level severity, const name<N>& context, const char* format,
                   arg_types&&... args) {
            prefix(severity, context.text, name<N>::length);
            write_format(severity, format, static_cast<arg_types&&>(args)...);
            put(severity, '\n');
        }
    } // namespace detail

    /// A log source: the name its lines are filed under, and the parent
    /// those names hang from.  Both are types, so the whole name is composed
    /// once, at compile time — a context costs nothing at the call site, and
    /// nesting one costs nothing at all.
    ///
    ///     using dev  = astia::log::context<"dev">;
    ///     using uart = dev::child<"uart">;        // lines say dev::uart
    ///
    template <name Name, typename Parent = void>
    struct context {
        /// The whole name: the parent's, a dot, and this one's.
        static constexpr auto full_name = detail::join<Name, Parent>();

        /// The source `Child` inside this one.
        template <name Child>
        using child = context<Child, context>;

        template <typename... arg_types>
        static void debug(const char* format, arg_types&&... args) {
            detail::print(level::debug, full_name, format,
                          static_cast<arg_types&&>(args)...);
        }

        template <typename... arg_types>
        static void info(const char* format, arg_types&&... args) {
            detail::print(level::info, full_name, format,
                          static_cast<arg_types&&>(args)...);
        }

        template <typename... arg_types>
        static void warning(const char* format, arg_types&&... args) {
            detail::print(level::warning, full_name, format,
                          static_cast<arg_types&&>(args)...);
        }

        template <typename... arg_types>
        static void error(const char* format, arg_types&&... args) {
            detail::print(level::error, full_name, format,
                          static_cast<arg_types&&>(args)...);
        }

        /// A line at a severity the caller carries rather than names.
        template <typename... arg_types>
        static void print(level severity, const char* format, arg_types&&... args) {
            detail::print(severity, full_name, format, static_cast<arg_types&&>(args)...);
        }
    };

    /// The kernel's own context: the boot path, and a panic from anywhere.
    /// A module names itself instead of hanging under this.
    using root = context<"core">;

    template <typename... arg_types>
    void debug(const char* format, arg_types&&... args) {
        root::debug(format, static_cast<arg_types&&>(args)...);
    }

    template <typename... arg_types>
    void info(const char* format, arg_types&&... args) {
        root::info(format, static_cast<arg_types&&>(args)...);
    }

    template <typename... arg_types>
    void warning(const char* format, arg_types&&... args) {
        root::warning(format, static_cast<arg_types&&>(args)...);
    }

    template <typename... arg_types>
    void error(const char* format, arg_types&&... args) {
        root::error(format, static_cast<arg_types&&>(args)...);
    }

    template <typename... arg_types>
    void print(level severity, const char* format, arg_types&&... args) {
        root::print(severity, format, static_cast<arg_types&&>(args)...);
    }

} // namespace astia::log
