#include "../src/mod/module.hpp"
#include "../src/mod/module_driver.hpp"
#include "support/test.hpp"
#include <astia/hw/pci.hpp>
#include <astia/mm/share.hpp>

#include <cstdint>
#include <cstdlib>
#include <new>

namespace astia::mm::heap {

    optional<u8*> alloc(usize size) {
        return static_cast<u8*>(std::malloc(size == 0 ? 1 : size));
    }

    void free(void* value) {
        std::free(value);
    }

}

namespace astia::mod {

    loaded_module test_module{};

    const loaded_module* current_module() {
        return &test_module;
    }

    bool register_pci_driver(const astia_module_pci_driver& ops);

}

namespace astia::arch::cpu {

    bool check_interrupts() {
        return true;
    }

    void interrupts() {}
    void nointerrupts() {}

}

namespace {

    unsigned pci_probes     = 0;
    unsigned pci_removes    = 0;
    unsigned pci_matches    = 0;
    unsigned selftest_calls = 0;
    int      pci_context    = 0;

    int block_match(const astia_module_pci_device*) {
        return 0;
    }

    int block_probe(const astia_module_pci_device*, void**) {
        return 0;
    }

    uint64_t block_size(void*) {
        return 1;
    }

    uint32_t block_block_size(void*) {
        return 1;
    }

    int64_t block_read(void*, void*, uint32_t, uint64_t) {
        return 0;
    }

    int net_probe(const astia_module_pci_device*, astia::net::netdev**) {
        return 0;
    }

    int unexpected_selftest() {
        ++selftest_calls;
        return 1;
    }

    int pci_match(const astia_module_pci_device*) {
        ++pci_matches;
        return 1;
    }

    int pci_probe(const astia_module_pci_device*, void** context) {
        ++pci_probes;
        *context = &pci_context;
        return 0;
    }

    void pci_remove(void* context) {
        if (context == &pci_context)
            ++pci_removes;
    }

    const astia_module_pci_block_driver block_driver{
        "lifecycle-block",
        "/dev/lifecycle-block",
        block_match,
        block_probe,
        nullptr,
        block_size,
        block_block_size,
        block_read,
        unexpected_selftest,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
    };

    const astia::module::net_driver net_driver{
        "lifecycle-net", block_match, net_probe, nullptr, unexpected_selftest,
    };

    const astia_module_pci_driver pci_driver{
        "lifecycle-pci",
        pci_match,
        pci_probe,
        pci_remove,
    };

}

ASTIA_TEST(module_driver_skips_teardown_registrations_in_selftests) {
    const astia::mod::loaded_module& module = astia::mod::test_module;
    ASTIA_CHECK(astia::mod::register_pci_block_driver(block_driver));
    ASTIA_CHECK(astia::mod::register_pci_net_driver(net_driver));
    ASTIA_CHECK(astia::mod::run_registration_selftests_for_test());
    ASTIA_CHECK(selftest_calls == 2);
    ASTIA_CHECK(astia::mod::teardown_module(module));
    ASTIA_CHECK(astia::mod::run_registration_selftests_for_test());
    ASTIA_CHECK(selftest_calls == 2);
    ASTIA_CHECK(astia::mod::teardown_module(module));
}

ASTIA_TEST(module_driver_tears_down_after_probe_before_unload) {
    astia::hw::drivers().unbind_all();
    astia::hw::pci_devices().clear();
    pci_probes  = 0;
    pci_removes = 0;
    pci_matches = 0;

    ASTIA_CHECK(astia::mod::register_pci_driver(pci_driver));
    auto device = astia::hw::pci_device{};
    device.bdf  = astia::hw::pci_bdf(0, 1, 0);
    ASTIA_CHECK(astia::hw::pci_devices().add(device) != nullptr);
    ASTIA_CHECK(astia::hw::pci_devices().find(device.bdf) != nullptr);
    auto candidate =
        astia::mm::shptr<astia::hw::device_info>(new astia::hw::pci_device_info(device));
    ASTIA_CHECK(astia::hw::pci_device_info::get(*candidate) != nullptr);
    ASTIA_CHECK(astia::hw::pci_device_info::get(*candidate)->device().bdf == device.bdf);
    ASTIA_CHECK(astia::hw::drivers().driver_count() == 1);
    ASTIA_CHECK(astia::hw::drivers().driver_at(0)->match(*candidate));
    ASTIA_CHECK(static_cast<bool>(candidate));

    ASTIA_CHECK(astia::hw::drivers().bind(
        static_cast<astia::mm::shptr<astia::hw::device_info>&&>(candidate)));
    ASTIA_CHECK(pci_matches == 2);
    ASTIA_CHECK(pci_probes == 1);
    ASTIA_CHECK(astia::hw::drivers().device_count() == 1);
    ASTIA_CHECK(astia::mod::teardown_module(astia::mod::test_module));
    ASTIA_CHECK(pci_removes == 1);
    ASTIA_CHECK(astia::hw::drivers().device_count() == 0);
    ASTIA_CHECK(astia::mod::run_registration_selftests_for_test());
    astia::hw::pci_devices().clear();
}
