/*
 * Startup-frame probe for execve argument limits.
 *
 * Run through /bin/exec_argv_probe by init_libc's exec argument test:
 *   argc == 1        empty vector was accepted (the kernel supplies argv[0])
 *   argv[1]=="long300" and argv[2] is 300 'x' bytes
 *   argv[1]=="many200" and argv[2..201] are "entry-000".."entry-199"
 * Exits 0 when the frame matches, a small step number otherwise.
 */
#include <astia/api/syscalls.h>

static unsigned long text_length(const char* text) {
    unsigned long length = 0;

    while (text[length] != '\0') {
        ++length;
    }

    return length;
}

static int text_equal(const char* left, const char* right) {
    unsigned long i = 0;

    while (left[i] != '\0' && left[i] == right[i]) {
        ++i;
    }

    return left[i] == right[i];
}

static int long_argument_ok(unsigned long argc, char* const* argv) {
    if (argc != 3)
        return 2;

    if (text_length(argv[2]) != 300)
        return 3;

    for (unsigned long i = 0; i < 300; ++i) {
        if (argv[2][i] != 'x')
            return 4;
    }

    return 0;
}

static void expected_entry(char* out, unsigned long index) {
    out[0] = 'e';
    out[1] = 'n';
    out[2] = 't';
    out[3] = 'r';
    out[4] = 'y';
    out[5] = '-';
    out[6] = (char) ('0' + (index / 100) % 10);
    out[7] = (char) ('0' + (index / 10) % 10);
    out[8] = (char) ('0' + index % 10);
    out[9] = '\0';
}

static int many_arguments_ok(unsigned long argc, char* const* argv) {
    if (argc != 202)
        return 5;

    for (unsigned long i = 0; i < 200; ++i) {
        char expected[16];

        expected_entry(expected, i);

        if (!text_equal(argv[2 + i], expected))
            return 6;
    }

    return 0;
}

static int frame_status(const unsigned long* frame) {
    const unsigned long argc = frame[0];

    char* const* argv = (char* const*) (frame + 1);

    if (argc == 1)
        return 0;

    if (argc >= 2 && text_equal(argv[1], "long300"))
        return long_argument_ok(argc, argv);

    if (argc >= 2 && text_equal(argv[1], "many200"))
        return many_arguments_ok(argc, argv);

    return 1;
}

long c_main(const unsigned long* frame) {
    astia_exit(frame_status(frame));
}

__asm__(".globl _start\n"
        "_start:\n"
        "    movq %rsp, %rdi\n"
        "    call c_main\n"
        "    ud2\n");
