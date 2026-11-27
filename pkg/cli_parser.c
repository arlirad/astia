#include <stdio.h>
#include <string.h>

extern int opterr;
extern int optind;
extern int optopt;
int        getopt(int argc, char* const argv[], const char* optstring);

static int error_text(const char* text) {
    fputs(text, stderr);
    fputc('\n', stderr);

    return 1;
}

static int error_option(const char* option, const char* suffix) {
    fputs(option, stderr);
    fputs(suffix, stderr);
    fputc('\n', stderr);

    return 1;
}

static int error_conflicting_flags(int actions) {
    fputs("conflicting short flags: -", stderr);

    if ((actions & 1) != 0)
        fputc('s', stderr);

    if ((actions & 2) != 0)
        fputc('i', stderr);

    if ((actions & 8) != 0)
        fputc('u', stderr);

    if ((actions & 4) != 0)
        fputc('r', stderr);

    fputc('\n', stderr);

    return 1;
}

static int set_long_option(int argc, char* const argv[], int* index, const char** root,
                           const char** repo, const char** arch) {
    const char* value = argv[*index];
    const char* name  = value + 2;
    const char* equal = strchr(name, '=');

    size_t name_length = equal == NULL ? strlen(name) : (size_t) (equal - name);

    const char* argument = equal == NULL ? NULL : equal + 1;

    if (name_length == 4 && strncmp(name, "root", name_length) == 0)
        argument = argument != NULL    ? argument
                   : *index + 1 < argc ? argv[++*index]
                                       : NULL;
    else if (name_length == 4 && strncmp(name, "repo", name_length) == 0)
        argument = argument != NULL    ? argument
                   : *index + 1 < argc ? argv[++*index]
                                       : NULL;
    else if (name_length == 4 && strncmp(name, "arch", name_length) == 0)
        argument = argument != NULL    ? argument
                   : *index + 1 < argc ? argv[++*index]
                                       : NULL;
    else
        return error_option("unknown option: ", value);

    if (argument == NULL || argument[0] == '\0') {
        fputs(value, stderr);
        fputs(" requires a value\n", stderr);

        return 1;
    }

    if (name_length == 4 && strncmp(name, "root", name_length) == 0)
        *root = argument;
    else if (name_length == 4 && strncmp(name, "repo", name_length) == 0)
        *repo = argument;
    else
        *arch = argument;

    return 0;
}

static int emit(const char* operation, const char* root, const char* repo,
                const char* arch, int operand_count, char* const operands[]) {
    fputs(operation, stdout);
    fputc('\0', stdout);
    fputs(root == NULL ? "" : root, stdout);
    fputc('\0', stdout);
    fputs(repo == NULL ? "" : repo, stdout);
    fputc('\0', stdout);
    fputs(arch == NULL ? "" : arch, stdout);
    fputc('\0', stdout);

    for (int index = 0; index < operand_count; ++index) {
        fputs(operands[index], stdout);
        fputc('\0', stdout);
    }

    return fflush(stdout) == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
    const char* root = NULL;
    const char* repo = NULL;
    const char* arch = NULL;
    char*       option_argv[argc + 1];
    char*       operands[argc + 1];

    int option_argc   = 1;
    int operand_count = 0;
    int ended         = 0;
    int actions       = 0;
    int option;

    const char* operation = NULL;

    option_argv[0] = argv[0];

    for (int index = 1; index < argc; ++index) {
        if (!ended && strcmp(argv[index], "--") == 0) {
            ended = 1;

            continue;
        }

        if (!ended && argv[index][0] == '-' && argv[index][1] == '-' &&
            argv[index][2] != '\0') {
            if (set_long_option(argc, argv, &index, &root, &repo, &arch) != 0)
                return 1;

            continue;
        }

        if (!ended && argv[index][0] == '-' && argv[index][1] != '\0') {
            option_argv[option_argc++] = argv[index];

            continue;
        }

        if (!ended &&
            (strcmp(argv[index], "sync") == 0 || strcmp(argv[index], "install") == 0 ||
             strcmp(argv[index], "remove") == 0 || strcmp(argv[index], "list") == 0 ||
             strcmp(argv[index], "status") == 0 || strcmp(argv[index], "upgrade") == 0)) {
            if (operation != NULL)
                return error_text("conflicting operations");

            operation = argv[index];

            continue;
        }

        operands[operand_count++] = argv[index];
    }

    option_argv[option_argc] = NULL;

    optind = 1;
    opterr = 0;

    while ((option = getopt(option_argc, option_argv, "siru")) != -1) {
        if (option == '?') {
            char message[32];
            int  length =
                snprintf(message, sizeof message, "unknown short flag: -%c", optopt);

            if (length < 0 || (size_t) length >= sizeof message)
                return error_text("unknown short flag");

            return error_text(message);
        }

        if (option == 's')
            actions |= 1;
        else if (option == 'i')
            actions |= 2;
        else if (option == 'r')
            actions |= 4;
        else if (option == 'u')
            actions |= 8;
    }

    if (operation != NULL && actions != 0)
        return error_text("conflicting operations");

    if (operation == NULL && actions == 1) {
        operation = "sync";
    }
    else if (operation == NULL && actions == 2) {
        operation = "install";
    }
    else if (operation == NULL && actions == 4) {
        operation = "remove";
    }
    else if (operation == NULL && actions == 3) {
        operation = "sync-install";
    }
    else if (operation == NULL && actions == 9) {
        operation = "sync-upgrade";
    }
    else if (operation == NULL && actions == 8) {
        operation = "upgrade";
    }
    else if (operation == NULL) {
        if (actions != 0)
            return error_conflicting_flags(actions);

        return error_text("invalid operation or flag combination");
    }

    if (strcmp(operation, "sync-install") == 0 &&
        (operand_count < 1 || operand_count > 2))
        return error_text("-si requires NAME and optionally VERSION");

    if (strcmp(operation, "install") == 0 && (operand_count < 1 || operand_count > 2))
        return error_text("-i/install requires NAME and optionally VERSION");

    if (strcmp(operation, "remove") == 0 && operand_count != 1)
        return error_text("-r/remove requires NAME");

    if ((strcmp(operation, "upgrade") == 0 || strcmp(operation, "sync") == 0 ||
         strcmp(operation, "sync-upgrade") == 0) &&
        operand_count != 0)
        return strcmp(operation, "sync-upgrade") == 0
                   ? error_text("-su does not take package names")
               : strcmp(operation, "upgrade") == 0
                   ? error_text("-u/upgrade does not take package names")
                   : error_text("-s/sync does not take package names");

    if ((strcmp(operation, "list") == 0 || strcmp(operation, "status") == 0) &&
        operand_count != 0)
        return error_text("operation does not take arguments");

    return emit(operation, root, repo, arch, operand_count, operands);
}
