// Deliberately not a valid module: the loader must reject a synchronous
// `int module_main()` because the kernel-module entry ABI is task-shaped.
int module_main() {
    return 0;
}
