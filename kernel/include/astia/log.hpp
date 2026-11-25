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

        inline constexpr u32 record_capacity = 512;

        /// One record, formatted before it is handed to the writer so a
        /// concurrent log source cannot interleave inside it.  A record
        /// longer than the buffer is truncated and closed with `...`.
        class record_builder {
            public:
            void append(char c) {
                if (_length < record_capacity) {
                    _text[_length++] = c;
                }
                else {
                    _overflow = true;
                }
            }

            void append(const char* text, u32 length) {
                for (u32 i = 0; i < length; ++i) {
                    append(text[i]);
                }
            }

            /// Close the record with a newline, or with `...\n` when the
            /// body did not fit.
            void close() {
                if (!_overflow) {
                    append('\n');
                    if (!_overflow) {
                        return;
                    }
                }
                constexpr u32 ellipsis = 4;
                _length                = record_capacity - ellipsis;
                _text[_length + 0]     = '.';
                _text[_length + 1]     = '.';
                _text[_length + 2]     = '.';
                _text[_length + 3]     = '\n';
                _length                = record_capacity;
            }

            const char* data() const {
                return _text;
            }
            u32 length() const {
                return _length;
            }

            private:
            char _text[record_capacity]{};
            u32  _length   = 0;
            bool _overflow = false;
        };

        /// Write one complete record.  The writer serialises records so
        /// that they cannot be interleaved by another CPU or an interrupt.
        void emit(level severity, const char* data, u32 length);

        /// The `[time LEVEL] context: ` a line opens with.
        void prefix(record_builder& record, level severity, const char* context,
                    u32 length);

        inline void number(record_builder& record, u64 value, unsigned base, bool upper) {
            char        buffer[32];
            u32         count  = 0;
            const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
            do {
                buffer[count++] = digits[value % base];
                value /= base;
            } while (value != 0 && count < sizeof(buffer));
            while (count != 0) {
                record.append(buffer[--count]);
            }
        }

        inline void signed_number(record_builder& record, i64 value) {
            if (value < 0) {
                record.append('-');
                number(record, static_cast<u64>(-(value + 1)) + 1, 10, false);
            }
            else {
                number(record, static_cast<u64>(value), 10, false);
            }
        }

        inline void write_arg(record_builder& record, char spec, const char* value) {
            if (spec == 's') {
                if (value == nullptr) {
                    value = "(null)";
                }
                while (*value != '\0') {
                    record.append(*value++);
                }
                return;
            }
            record.append('0');
            record.append('x');
            number(record, reinterpret_cast<u64>(value), 16, false);
        }

        inline void write_arg(record_builder& record, char spec, char value) {
            if (spec == 'c') {
                record.append(value);
            }
            else if (spec == 'd' || spec == 'i') {
                signed_number(record, value);
            }
            else {
                number(record, static_cast<u64>(static_cast<unsigned char>(value)),
                       spec == 'x' || spec == 'X' ? 16 : 10, spec == 'X');
            }
        }

        template <typename value_type>
        void write_arg(record_builder& record, char spec, value_type value) {
            static_assert(std::is_integral_v<value_type> || std::is_pointer_v<value_type>,
                          "unsupported astia::log format argument");
            if constexpr (std::is_pointer_v<value_type>) {
                record.append('0');
                record.append('x');
                number(record, reinterpret_cast<u64>(value), 16, false);
            }
            else if constexpr (std::is_signed_v<value_type>) {
                if (spec == 'd' || spec == 'i') {
                    signed_number(record, static_cast<i64>(value));
                }
                else {
                    number(record, static_cast<u64>(value),
                           spec == 'x' || spec == 'X' ? 16 : 10, spec == 'X');
                }
            }
            else {
                number(record, static_cast<u64>(value),
                       spec == 'x' || spec == 'X' ? 16 : 10, spec == 'X');
            }
        }

        inline void write_format(record_builder& record, const char* format) {
            while (*format != '\0') {
                if (*format == '%' && format[1] == '%') {
                    record.append('%');
                    format += 2;
                }
                else {
                    record.append(*format++);
                }
            }
        }

        template <typename value_type, typename... rest_types>
        void write_format(record_builder& record, const char* format, value_type&& value,
                          rest_types&&... rest) {
            while (*format != '\0') {
                if (*format != '%') {
                    record.append(*format++);
                    continue;
                }
                ++format;
                if (*format == '%') {
                    record.append('%');
                    ++format;
                    continue;
                }

                while (*format == 'l' || *format == 'z') {
                    ++format;
                }
                if (*format == '\0') {
                    record.append('%');
                    return;
                }
                const char spec = *format++;
                write_arg(record, spec, static_cast<value_type&&>(value));
                write_format(record, format, static_cast<rest_types&&>(rest)...);
                return;
            }
        }

        template <u32 N, typename... arg_types>
        void print(level severity, const name<N>& context, const char* format,
                   arg_types&&... args) {
            record_builder record;
            prefix(record, severity, context.text, name<N>::length);
            write_format(record, format, static_cast<arg_types&&>(args)...);
            record.close();
            emit(severity, record.data(), record.length());
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
