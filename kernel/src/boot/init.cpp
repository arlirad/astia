#include <astia/boot/init.hpp>

namespace astia::boot {
    namespace {
        constexpr char default_init[] = "/sbin/init";

        bool whitespace(char value) {
            return value == ' ' || value == '\t' || value == '\n' || value == '\r';
        }

        bool equal_token(const char* first, usize length, const char* expected) {
            usize index = 0;

            while (index < length && expected[index] != '\0' &&
                   first[index] == expected[index]) {
                ++index;
            }

            return index == length && expected[index] == '\0';
        }

        bool valid_path(const char* path, usize length) {
            if (length == 0 || path[0] != '/')
                return false;

            usize component_start = 1;

            for (usize index = 1; index <= length; ++index) {
                if (index != length && path[index] != '/')
                    continue;

                const usize component_length = index - component_start;

                if (component_length == 0 && index != length)
                    return false;

                if ((component_length == 1 && path[component_start] == '.') ||
                    (component_length == 2 && path[component_start] == '.' &&
                     path[component_start + 1] == '.'))
                    return false;

                component_start = index + 1;
            }

            return true;
        }
    } // namespace

    init_selection select_init(const char* command_line) {
        init_selection result{};

        usize default_length = sizeof(default_init) - 1;

        for (usize index = 0; index < default_length; ++index) {
            result.path[index] = default_init[index];
        }

        if (command_line == nullptr)
            return result;

        bool selected = false;

        const char* cursor = command_line;

        while (*cursor != '\0') {
            while (whitespace(*cursor)) {
                ++cursor;
            }

            if (*cursor == '\0')
                break;

            const char* token = cursor;

            while (*cursor != '\0' && !whitespace(*cursor)) {
                ++cursor;
            }

            const usize token_length = static_cast<usize>(cursor - token);

            if (equal_token(token, token_length, "--init")) {
                if (selected) {
                    result.error = init_option_error::duplicate;

                    return result;
                }

                selected = true;

                while (whitespace(*cursor)) {
                    ++cursor;
                }

                if (*cursor == '\0') {
                    result.error = init_option_error::missing_path;

                    return result;
                }

                const char* path = cursor;

                usize path_length = 0;

                while (*cursor != '\0' && !whitespace(*cursor) &&
                       path_length < init_path_capacity) {
                    ++cursor;
                    ++path_length;
                }

                if (path_length >= init_path_capacity) {
                    result.error = init_option_error::path_too_long;

                    return result;
                }

                if (!valid_path(path, path_length)) {
                    result.error = init_option_error::invalid_path;

                    return result;
                }

                for (usize index = 0; index < path_length; ++index) {
                    result.path[index] = path[index];
                }

                result.path[path_length] = '\0';
            }
            else if (token_length >= 7 && equal_token(token, 7, "--init=")) {
                result.error = init_option_error::invalid_path;

                return result;
            }
        }

        return result;
    }
} // namespace astia::boot
