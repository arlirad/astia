extern "C" void lifecycle_record(int);
extern "C" void lifecycle_exit_request();

__attribute__((constructor(101))) void lifecycle_constructor_exit() {
    lifecycle_record(301);
    lifecycle_exit_request();
}

__attribute__((constructor(102))) void lifecycle_constructor_after_exit() {
    lifecycle_record(302);
}

__attribute__((destructor(101))) void lifecycle_finalizer_exit() {
    lifecycle_record(401);
}

extern "C" int module_main() {
    lifecycle_record(303);
    return 53;
}
