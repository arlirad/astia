#include "common.h"

extern char** environ;

int main(void) {
    if (environ == 0)
        return 0;
    for (char** item = environ; *item != 0; ++item) {
        if (cu_puts(*item) < 0 || cu_puts("\n") < 0)
            return 1;
    }
    return 0;
}
