extern "C" int  atexit(void (*)(void));
extern "C" void lifecycle_record(int);

static void noop() {}

int module_main() {
    int failures = 0;
    for (int i = 0; i < 64; ++i)
        failures += atexit(noop) != 0;
    failures += atexit(noop) == 0;
    lifecycle_record(400 + failures);
    return failures;
}
