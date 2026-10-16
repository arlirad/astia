// A minimal linked executable, compiled at build time into both an ET_DYN (PIE)
// and an ET_EXEC image and embedded into the test (see tests/CMakeLists.txt).
//
// The `static int* p = &counter` initializer is the interesting part: in a PIE
// the stored pointer must be fixed up at load time (R_X86_64_RELATIVE), so
// `return *p` only yields the right answer if the loader applied that dynamic
// relocation. counter starts at 40; each call bumps it by 2, so fixture_entry
// returns 42 then 44.

static int  counter = 40;
static int* p       = &counter;

int fixture_entry(void) {
    counter += 2;
    return *p;
}
