#include <unistd.h>

#include <stddef.h>

char* optarg;
int   opterr = 1;
int   optind = 1;
int   optopt;

static const char* next_option;
static int         next_index;
static int         stopped;
static int         previous_optind = 1;
static int         state_argc;
static char* const* state_argv;

static int leading_colon(const char* optstring) {
    return optstring != NULL && optstring[0] == ':';
}

static const char* option_spec(const char* optstring, int option) {
    const char* cursor = optstring;
    if (cursor == NULL)
        return NULL;
    if (*cursor == ':')
        ++cursor;
    while (*cursor != '\0') {
        if ((unsigned char) *cursor == (unsigned char) option)
            return cursor;
        ++cursor;
    }
    return NULL;
}

static void report_error(int missing, int option) {
    static const char missing_prefix[] = "getopt: option requires an argument -- '";
    static const char unknown_prefix[] = "getopt: illegal option -- '";
    static const char suffix[] = "'\n";
    const char*      prefix = missing ? missing_prefix : unknown_prefix;
    size_t           prefix_length = 0;
    size_t           suffix_length = sizeof suffix - 1;
    char             value = (char) option;

    while (prefix[prefix_length] != '\0')
        ++prefix_length;
    (void) write(STDERR_FILENO, prefix, prefix_length);
    (void) write(STDERR_FILENO, &value, 1);
    (void) write(STDERR_FILENO, suffix, suffix_length);
}

int getopt(int argc, char* const argv[], const char* optstring) {
    const char* specification;
    int         option;
    int         index;

    optarg = NULL;
    if (argv != state_argv || argc != state_argc || optind != previous_optind) {
        next_option = NULL;
        stopped = 0;
    }
    state_argv = argv;
    state_argc = argc;

    if (stopped) {
        previous_optind = optind;
        return -1;
    }
    if (argv == NULL || argc <= 0 || optind < 1 || optind >= argc || argv[optind] == NULL) {
        previous_optind = optind;
        return -1;
    }

    if (next_option == NULL) {
        if (argv[optind][0] != '-' || argv[optind][1] == '\0') {
            previous_optind = optind;
            return -1;
        }
        if (argv[optind][1] == '-' && argv[optind][2] == '\0') {
            ++optind;
            stopped = 1;
            previous_optind = optind;
            return -1;
        }
        next_option = argv[optind] + 1;
        next_index = optind;
    }

    index = next_index;
    option = (unsigned char) *next_option++;
    specification = option_spec(optstring, option);
    if (specification == NULL) {
        optopt = option;
        if (*next_option == '\0') {
            next_option = NULL;
            optind = index + 1;
        }
        if (opterr != 0 && !leading_colon(optstring))
            report_error(0, option);
        previous_optind = optind;
        return '?';
    }

    if (specification[1] != ':') {
        if (*next_option == '\0') {
            next_option = NULL;
            optind = index + 1;
        }
        previous_optind = optind;
        return option;
    }

    if (*next_option != '\0') {
        optarg = (char*) next_option;
        next_option = NULL;
        optind = index + 1;
        previous_optind = optind;
        return option;
    }

    next_option = NULL;
    optind = index + 2;
    if (index + 1 < argc && argv[index + 1] != NULL) {
        optarg = argv[index + 1];
        previous_optind = optind;
        return option;
    }

    optopt = option;
    optarg = NULL;
    if (opterr != 0 && !leading_colon(optstring))
        report_error(1, option);
    previous_optind = optind;
    return leading_colon(optstring) ? ':' : '?';
}
