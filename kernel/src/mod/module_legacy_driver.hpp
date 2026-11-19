#pragma once

#include <astia/module_abi.hpp>

#include "module.hpp"

namespace astia::mod {

    bool register_pci_block_driver(const astia_module_pci_block_driver& ops);
    bool register_pci_net_driver(const astia::module::net_driver& ops);
    bool run_legacy_selftests();
    void release_legacy_module(const loaded_module* owner) __attribute__((weak));

}
