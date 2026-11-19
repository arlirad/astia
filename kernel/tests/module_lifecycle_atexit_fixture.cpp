extern "C" void lifecycle_record(int);

struct lifecycle_object {
    ~lifecycle_object() {
        lifecycle_record(301);
    }
};

lifecycle_object object;

int module_main() {
    lifecycle_record(300);
    return 38;
}
