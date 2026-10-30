#include "common.h"

static int print_field(const char* value, int* printed) {
    if (*printed && cu_puts(" ") < 0)
        return -1;
    if (cu_puts(value) < 0)
        return -1;
    *printed = 1;
    return 0;
}

int main(int argc, char** argv) {
    int all     = 0;
    int printed = 0;
    for (int i = 1; i < argc; ++i) {
        const char* option = argv[i];
        if (cu_is_help(option))
            return cu_help("uname", "[OPTION]...", "Print system information.");
        if (cu_is_version(option))
            return cu_version("uname");
        if (cu_arg_is(option, "-a", "--all")) {
            all = 1;
            continue;
        }
        if (option[0] != '-' || option[1] == '\0')
            return cu_usage("uname", "[OPTION]...");
        for (int j = 1; option[j] != '\0'; ++j) {
            const char* value = 0;
            switch (option[j]) {
            case 's':
                value = "Astia";
                break;
            case 'n':
                value = "astia";
                break;
            case 'r':
                value = "0.1";
                break;
            case 'v':
                value = "Astia kernel";
                break;
            case 'm':
                value = "x86_64";
                break;
            default:
                return cu_error("uname", "unknown option");
            }
            if (print_field(value, &printed) < 0)
                return 1;
        }
    }
    if (all) {
        printed = 0;
        if (print_field("Astia", &printed) < 0 || print_field("astia", &printed) < 0 ||
            print_field("0.1", &printed) < 0 ||
            print_field("Astia kernel", &printed) < 0 ||
            print_field("x86_64", &printed) < 0)
            return 1;
    }
    else if (!printed && print_field("Astia", &printed) < 0) {
        return 1;
    }
    return cu_puts("\n") < 0;
}
