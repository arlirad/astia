/* dlopen fixture: a constructor proves initializers run before dlopen
 * returns, and two exported calls prove dlsym handles and the image stay
 * usable across a second handle. */
static int dl_fixture_inited;

__attribute__((constructor)) static void dl_fixture_construct(void) {
    dl_fixture_inited = 1;
}

int dl_answer(void) {
    return 42;
}

int dl_inited(void) {
    return dl_fixture_inited;
}
