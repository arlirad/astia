#include <astia/core/name.hpp>
#include <astia/core/spinlock.hpp>

namespace astia::core {
    namespace {
        constexpr usize max_pattern    = 32;
        constexpr usize max_patterns   = 32;
        constexpr usize letter_max_len = 16;

        struct source {
            char pattern[max_pattern]{};
            bool used  = false;
            u64  value = 0;
        };

        source         letter_sources[max_patterns]{};
        source         number_sources[max_patterns]{};
        core::spinlock name_lock;

        bool same(const char* left, const char* right) {
            while (*left != '\0' && *left == *right) {
                ++left;
                ++right;
            }

            return *left == *right;
        }

        bool copy_pattern(char* out, const char* pattern) {
            if (pattern == nullptr)
                return false;

            usize length = 0;

            while (pattern[length] != '\0') {
                if (length + 1 >= max_pattern)
                    return false;

                out[length] = pattern[length];

                ++length;
            }

            out[length] = '\0';

            return true;
        }

        source* acquire(source* table, const char* pattern) {
            if (pattern == nullptr)
                return nullptr;

            for (usize i = 0; i < max_patterns; ++i) {
                if (table[i].used && same(table[i].pattern, pattern))
                    return &table[i];
            }

            for (usize i = 0; i < max_patterns; ++i) {
                if (table[i].used)
                    continue;

                if (!copy_pattern(table[i].pattern, pattern))
                    return nullptr;

                table[i].used  = true;
                table[i].value = 0;

                return &table[i];
            }

            return nullptr;
        }

        /// Render `pattern` with `counter` in place of its single '%'.
        /// Returns the length written, or 0 when the pattern is malformed or
        /// the result does not fit `capacity`.
        usize render(char* buffer, usize capacity, const char* pattern,
                     const char* counter) {
            if (buffer == nullptr || capacity == 0 || pattern == nullptr)
                return 0;

            const char* percent = nullptr;

            usize length = 0;

            for (const char* cursor = pattern; *cursor != '\0'; ++cursor) {
                if (*cursor == '%') {
                    if (percent != nullptr)
                        return 0; // more than one counter position

                    percent = cursor;

                    continue;
                }

                ++length;
            }

            if (percent == nullptr)
                return 0;

            const usize prefix = static_cast<usize>(percent - pattern);
            const usize suffix = length - prefix;
            usize       count  = 0;

            for (const char* cursor = counter; *cursor != '\0'; ++cursor) {
                ++count;
            }

            if (prefix + count + suffix + 1 > capacity)
                return 0;

            for (usize i = 0; i < prefix; ++i) {
                buffer[i] = pattern[i];
            }

            for (usize i = 0; i < count; ++i) {
                buffer[prefix + i] = counter[i];
            }

            for (usize i = 0; i <= suffix; ++i) {
                buffer[prefix + count + i] = percent[1 + i];
            }

            return prefix + count + suffix;
        }

        usize render_decimal(char* out, u64 value) {
            char  reversed[24]{};
            usize count = 0;

            do {
                reversed[count++] = static_cast<char>('0' + value % 10);
                value /= 10;
            } while (value != 0);

            for (usize i = 0; i < count; ++i) {
                out[i] = reversed[count - 1 - i];
            }

            out[count] = '\0';

            return count;
        }

        usize render_letters(char* out, u64 value) {
            char  reversed[letter_max_len]{};
            usize count = 0;

            while (true) {
                reversed[count++] = static_cast<char>('a' + value % 26);
                value /= 26;
                if (value == 0)
                    break;

                --value;
            }

            for (usize i = 0; i < count; ++i) {
                out[i] = reversed[count - 1 - i];
            }

            out[count] = '\0';

            return count;
        }

        bool increment(source* table, char* buffer, usize capacity, const char* pattern,
                       bool letters) {
            core::spinlock_guard guard(name_lock);

            source* entry = acquire(table, pattern);
            if (entry == nullptr)
                return false;

            char counter[letter_max_len]{};

            if (letters)
                (void) render_letters(counter, entry->value);
            else
                (void) render_decimal(counter, entry->value);

            if (render(buffer, capacity, pattern, counter) == 0)
                return false;

            if (entry->value == ~u64{0})
                return false; // rendered, but the sequence is exhausted

            ++entry->value;

            return true;
        }
    } // namespace

    bool name_letter_increment(char* buffer, usize capacity, const char* pattern) {
        return increment(letter_sources, buffer, capacity, pattern, true);
    }

    bool name_number_increment(char* buffer, usize capacity, const char* pattern) {
        return increment(number_sources, buffer, capacity, pattern, false);
    }
} // namespace astia::core
