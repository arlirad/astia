extern "C" int  lifecycle_module_api_probe();
extern "C" void lifecycle_irq_entry();

int module_main() {
    return lifecycle_module_api_probe();
}
