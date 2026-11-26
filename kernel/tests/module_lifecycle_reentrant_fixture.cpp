extern "C" void lifecycle_reentrant_outer_handler();
extern "C" int  atexit(void (*)(void));

int module_main() {
    return atexit(lifecycle_reentrant_outer_handler);
}
