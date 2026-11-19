#pragma once

#include <astia/module_abi.hpp>

namespace astia::mod {

    struct loaded_module;

    const loaded_module* current_module();
    bool register_pci_block_driver(const astia_module_pci_block_driver& ops);
    bool register_pci_net_driver(const astia::module::net_driver& ops);
    bool teardown_module(const loaded_module& module);
    bool run_selftests();
#ifdef ASTIA_MODULE_HOST_TEST
    bool run_registration_selftests_for_test();
#endif

}
