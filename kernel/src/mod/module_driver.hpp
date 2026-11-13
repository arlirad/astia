#pragma once

#include <astia/module_abi.h>

namespace astia::mod {

    bool register_pci_block_driver(const astia_module_pci_block_driver& ops);
    bool run_selftests();

}
