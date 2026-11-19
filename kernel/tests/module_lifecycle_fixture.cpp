extern "C" void lifecycle_record(int);

struct lifecycle_object {
    lifecycle_object() {
        lifecycle_record(100);
    }

    ~lifecycle_object() {
        lifecycle_record(203);
    }
};

lifecycle_object lifecycle_instance;

__attribute__((constructor(101))) void lifecycle_constructor_one() {
    lifecycle_record(101);
}

__attribute__((constructor(102))) void lifecycle_constructor_two() {
    lifecycle_record(102);
}

__attribute__((destructor(101))) void lifecycle_finalizer_one() {
    lifecycle_record(201);
}

__attribute__((destructor(102))) void lifecycle_finalizer_two() {
    lifecycle_record(202);
}

extern "C" int module_main() {
    lifecycle_record(103);
    return 37;
}
