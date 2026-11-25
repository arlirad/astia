extern "C" void lifecycle_wait_for_release();

int module_main() {
    lifecycle_wait_for_release();
    return 19;
}
