namespace kernel_fixture {

    int add(int left, int right);
    int bias();

}

namespace {

    int constructor_value = 0;

    struct fixture_guard {
        fixture_guard() { constructor_value = 7; }
        ~fixture_guard() { constructor_value = 99; }
    } guard;

}

int module_main() {
    return kernel_fixture::add(20, 22) + kernel_fixture::bias() + constructor_value;
}
