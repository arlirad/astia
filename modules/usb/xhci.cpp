#include <astia/module_abi.hpp>

#include "xhci.hpp"

namespace astia::usb::xhci {

bool pci_match(const astia_module_pci_device* device) {
    return device != nullptr && device->class_code == 0x0c && device->subclass == 0x03 &&
           device->programming_interface == 0x30 && device->bar_count != 0 &&
           device->bar_present[0] != 0 && device->bar_io[0] == 0;
}

int pci_capability_probe(const astia_module_pci_device* device, probe_result& result) {
    if (!pci_match(device))
        return -19;
    if (astia_module_pci_map_bar(device->opaque, 0) != 0)
        return -19;
    const uint64_t mapped = astia_module_pci_bar_mapped(device->opaque, 0);
    const uint64_t size = astia_module_pci_bar_size(device->opaque, 0);
    if (mapped == 0 || size < 0x20)
        return -19;
    if (!probe(reinterpret_cast<volatile const uint8_t*>(mapped), size, result))
        return -95;
    return -95;
}

}

extern "C" int module_main() {
    return 0;
}
