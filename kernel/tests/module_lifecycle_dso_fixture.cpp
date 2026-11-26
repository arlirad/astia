extern "C" int  __cxa_atexit(void (*)(void*), void*, void*);
extern "C" void __cxa_finalize(void*);
extern "C" void lifecycle_record(int);

static int dso_a;
static int dso_b;

static void handler(void* argument) {
    lifecycle_record(argument == &dso_a ? 411 : argument == &dso_b ? 412 : 413);
}

int module_main() {
    if (__cxa_atexit(handler, &dso_a, &dso_a) != 0 ||
        __cxa_atexit(handler, &dso_b, &dso_b) != 0 ||
        __cxa_atexit(handler, nullptr, nullptr) != 0)
        return -1;

    __cxa_finalize(&dso_a);
    lifecycle_record(410);

    return 0;
}
