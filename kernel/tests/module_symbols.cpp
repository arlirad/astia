namespace kernel_fixture {

    int add(int left, int right);
    int bias();

}

namespace {

    int constructor_value = 0;

    void fixture_init() __attribute__((constructor));
    void fixture_fini() __attribute__((destructor));

    void fixture_init() {
        constructor_value = 7;
    }
    void fixture_fini() {
        constructor_value = 99;
    }

}

int module_main() {
    return kernel_fixture::add(20, 22) + kernel_fixture::bias() + constructor_value;
}
