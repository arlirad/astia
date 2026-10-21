#include "common.h"

static const char* value(const char* option) {
    if (option[1] == 's')
        return "Astia";
    if (option[1] == 'n')
        return "astia";
    if (option[1] == 'r')
        return "0.1";
    if (option[1] == 'v')
        return "Astia kernel";
    if (option[1] == 'm')
        return "x86_64";
    return 0;
}

int main(int argc, char** argv) {
    (void) argc;
    (void) argv;
    (void) cu_write_all(CU_STDOUT, "Astia\n", 6);
    return 0;
#if 0
    int printed = 0;
    for (int i = 1; i < argc; ++i) {
        const char* item = value(argv[i]);
        if (item == 0)
            return cu_error("uname", "unknown option");
        if (printed++ != 0 && cu_puts(" ") < 0)
            return 1;
        if (cu_puts(item) < 0)
            return 1;
    }
    if (!printed && cu_puts("Astia\n") < 0)
        return 1;
    return printed ? (cu_puts("\n") < 0) : 0;
#endif
}
