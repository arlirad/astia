#include "common.h"

#include <string.h>
#include <unistd.h>

struct ln_options {
    int symbolic;
    int force;
};

static int parse_option(const char* option, struct ln_options* options) {
    if (option[0] == '-' && option[1] == '-' && option[2] != '\0') {
        if (cu_arg_is(option, 0, "--symbolic")) {
            options->symbolic = 1;

            return 0;
        }

        if (cu_arg_is(option, 0, "--force")) {
            options->force = 1;

            return 0;
        }

        return -1;
    }

    if (option[0] != '-' || option[1] == '\0')
        return 1;

    for (int index = 1; option[index] != '\0'; ++index) {
        if (option[index] == 's')
            options->symbolic = 1;
        else if (option[index] == 'f')
            options->force = 1;
        else
            return -1;
    }

    return 0;
}

static int make_link(const char* target, const char* link_name, int symbolic) {
    return symbolic ? symlink(target, link_name) : link(target, link_name);
}

static int create_link(const char* target, const char* link_name,
                       const struct ln_options* options) {
    if (make_link(target, link_name, options->symbolic) == 0)
        return 0;

    if (!options->force || errno != EEXIST)
        return cu_error("ln", strerror(errno));

    if (unlink(link_name) < 0)
        return cu_error("ln", strerror(errno));

    if (make_link(target, link_name, options->symbolic) < 0)
        return cu_error("ln", strerror(errno));

    return 0;
}

int main(int argc, char** argv) {
    struct ln_options options = {0, 0};
    const char*       operands[2];

    int operand_count = 0;
    int parse_options = 1;

    for (int index = 1; index < argc; ++index) {
        const char* value = argv[index];

        if (parse_options && cu_arg_is(value, 0, "--")) {
            parse_options = 0;

            continue;
        }

        if (parse_options && cu_is_help(value))
            return cu_help("ln", "[OPTION]... TARGET LINK_NAME",
                           "Create a hard link to TARGET as LINK_NAME, or a symbolic "
                           "link with -s.");

        if (parse_options && cu_is_version(value))
            return cu_version("ln");

        int result = parse_options ? parse_option(value, &options) : 1;
        if (result < 0)
            return cu_usage("ln", "[OPTION]... TARGET LINK_NAME");

        if (result == 0)
            continue;

        if (operand_count == 2)
            return cu_usage("ln", "[OPTION]... TARGET LINK_NAME");

        operands[operand_count++] = value;
    }

    if (operand_count != 2)
        return cu_usage("ln", "[OPTION]... TARGET LINK_NAME");

    return create_link(operands[0], operands[1], &options) == 0 ? 0 : 1;
}
