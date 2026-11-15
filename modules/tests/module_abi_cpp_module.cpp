#include <astia/module_abi.hpp>

namespace {

    int match(const astia::module::pci_device* device) {
        return device != nullptr && device->vendor_id == 0x1234;
    }

    int probe(const astia::module::pci_device*, void**) {
        return 0;
    }

    uint64_t size(void*) {
        return 0;
    }

    uint32_t block_size(void*) {
        return 512;
    }

    int64_t read(void*, void*, uint32_t, uint64_t) {
        return 0;
    }

    const astia::module::block_driver driver{
        "abi-cpp-module", "/dev/abi-cpp-module", match, probe, nullptr, size, block_size,
        read, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

}

extern "C" int module_main() {
    return astia::module::register_block_driver(driver);
}
