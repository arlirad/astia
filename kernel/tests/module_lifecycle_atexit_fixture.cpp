extern "C" void lifecycle_record(int);
extern "C" int  atexit(void (*)(void));

static unsigned reentries;

static void reentrant() {
    lifecycle_record(302 + reentries++);
    if (reentries == 1)
        atexit(reentrant);
}

struct lifecycle_object {
    ~lifecycle_object() {
        lifecycle_record(301);
    }
};

lifecycle_object object;

int module_main() {
    lifecycle_record(300);
    reentries = 0;
    return atexit(reentrant) == 0 ? 38 : -1;
}
