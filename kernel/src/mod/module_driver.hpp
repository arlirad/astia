#pragma once

#include <astia/module_abi.h>

namespace astia::mod {

    bool register_pci_net_driver(const astia_module_pci_net_driver& ops);
    bool run_selftests();

}
