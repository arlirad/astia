using lifecycle_callback = void (*)();

static void lifecycle_priority_callback() {}

__attribute__((section(".init_array.65536"),
               used)) lifecycle_callback lifecycle_priority_entry =
    lifecycle_priority_callback;

extern "C" int module_main() {
    return 0;
}
