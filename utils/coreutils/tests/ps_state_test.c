#include "../src/ps_state.h"

#include <stdio.h>

typedef struct {
    uint32_t    state;
    char        expected;
    const char* name;
} state_case;

static int failures;

static void expect(const state_case* item) {
    char actual = cu_ps_state_letter(item->state);

    if (actual == item->expected)
        return;

    printf("ps state test failed: %s state %u got %c expected %c\n", item->name,
           (unsigned int) item->state, actual, item->expected);
    ++failures;
}

int main(void) {
    static const state_case cases[] = {
        {ASTIA_PROCESS_FRESH, 'F', "fresh"},
        {ASTIA_PROCESS_RUNNABLE, 'R', "runnable"},
        {ASTIA_PROCESS_STOPPING, 'T', "stopping"},
        {ASTIA_PROCESS_STOPPED, 'T', "stopped"},
        {ASTIA_PROCESS_ZOMBIE, 'Z', "zombie"},
        {ASTIA_PROCESS_DEAD, 'X', "dead"},
        {ASTIA_PROCESS_DEAD + 1, '?', "unknown"},
        {UINT32_MAX, '?', "maximum"},
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        expect(&cases[i]);
    }

    return failures != 0;
}
