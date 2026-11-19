extern "C" int  lifecycle_register_cxa(void (*)(void*), void*, void*);
extern "C" void lifecycle_finalize(void*);
extern "C" void lifecycle_record(int);

static int dso_a;
static int dso_b;

static void handler(void* argument) {
    lifecycle_record(argument == &dso_a ? 411 : 412);
}

int module_main() {
    if (lifecycle_register_cxa(handler, &dso_a, &dso_a) != 0 ||
        lifecycle_register_cxa(handler, &dso_b, &dso_b) != 0)
        return -1;
    lifecycle_finalize(&dso_a);
    lifecycle_record(410);
    return 0;
}
