namespace kernel_fixture {

    int add(int left, int right);
    int bias();

}

int module_main() {
    return kernel_fixture::add(20, 22) + kernel_fixture::bias();
}
