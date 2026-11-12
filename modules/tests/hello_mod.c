extern int kernel_add(int a, int b);

static int counter = 40;

int module_main(void) {
    counter += 2;
    return kernel_add(counter, 0);
}
