#include <astia/module.hpp>

#include "support/test.hpp"

#include <cstdint>

namespace {

    struct injection {
        bool     register_failure     = false;
        bool     unregister_failure   = false;
        bool     mask_failure         = false;
        bool     unmask_failure       = false;
        bool     message_failure      = false;
        bool     map_failure          = false;
        bool     config_read_failure  = false;
        bool     config_write_failure = false;
        unsigned unregister_count     = 0;
        unsigned disable_write_count  = 0;
        unsigned enable_write_count   = 0;
        uint16_t control              = 0;
    } state;

    alignas(16) uint32_t table[4]{};

    astia::hw::pci_device device() {
        astia::hw::pci_device result{};
        result.bdf                    = {0, 1, 0};
        result.bar_count              = 1;
        result.bars[0].present        = true;
        result.bars[0].size           = sizeof table;
        result.bars[0].mapped         = reinterpret_cast<astia::u64>(table);
        result.bars[0].mapped_valid   = true;
        result.interrupt.msix         = true;
        result.interrupt.vector_count = 1;
        result.interrupt.capability   = 0x50;
        result.interrupt.table_bar    = 0;
        result.interrupt.table_offset = 0;
        result.interrupt_blocker      = astia::hw::pci_interrupt_blocker::none;
        return result;
    }

    void reset() {
        state = {};
        for (unsigned& value : table)
            value = 0;
    }

    void check_released(astia::module::msix_route& route) {
        ASTIA_CHECK(!route.active());
        ASTIA_CHECK(!route.registered());
        ASTIA_CHECK(state.unregister_count == 1);
        ASTIA_CHECK((state.control & (1u << 15)) == 0);
        ASTIA_CHECK(route.release());
    }

    void noop_interrupt(void*) {}

}

namespace astia::arch {

    bool register_interrupt_vector(interrupt_vector_handler, void*,
                                   interrupt_vector& route) {
        if (state.register_failure)
            return false;
        route = {48, 7};
        return true;
    }

    bool unregister_interrupt_vector(interrupt_vector) {
        ++state.unregister_count;
        return !state.unregister_failure;
    }

    bool mask_interrupt_vector(interrupt_vector) {
        return !state.mask_failure;
    }

    bool unmask_interrupt_vector(interrupt_vector) {
        return !state.unmask_failure;
    }

    bool interrupt_vector_message(interrupt_vector, msi_message& message) {
        if (state.message_failure)
            return false;
        message = {0xfee00000u, 0, 48};
        return true;
    }

}

namespace astia::hw {

    bool pci_map_bar(pci_device&, u8) {
        return !state.map_failure;
    }

    bool pci_unmap_bar(pci_device&, u8) {
        return true;
    }

    bool pci_config_read16(pci_bdf, u16, u16& value) {
        if (state.config_read_failure)
            return false;
        value = state.control;
        return true;
    }

    bool pci_config_write16(pci_bdf, u16, u16 value) {
        if (state.config_write_failure)
            return false;
        state.control = value;
        if ((value & (1u << 15)) != 0)
            ++state.enable_write_count;
        else
            ++state.disable_write_count;
        return true;
    }

}

ASTIA_TEST(msix_register_failure_leaves_no_route) {
    reset();
    state.register_failure        = true;
    auto                      pci = device();
    astia::module::msix_route route;
    ASTIA_CHECK(!route.request(pci, noop_interrupt, nullptr));
    ASTIA_CHECK(!route.active());
    ASTIA_CHECK(state.unregister_count == 0);
    ASTIA_CHECK(state.disable_write_count == 0);
}

ASTIA_TEST(msix_registration_stage_failure_unwinds_route) {
    reset();
    state.message_failure         = true;
    auto                      pci = device();
    astia::module::msix_route route;
    ASTIA_CHECK(!route.request(pci, noop_interrupt, nullptr));
    ASTIA_CHECK(!route.active());
    ASTIA_CHECK(!route.registered());
    ASTIA_CHECK(state.unregister_count == 1);
    ASTIA_CHECK((state.control & (1u << 15)) == 0);
}

ASTIA_TEST(msix_program_failure_releases_registered_route) {
    reset();
    state.mask_failure            = true;
    auto                      pci = device();
    astia::module::msix_route route;
    ASTIA_CHECK(route.request(pci, noop_interrupt, nullptr));
    ASTIA_CHECK(!route.program());
    ASTIA_CHECK(route.release());
    check_released(route);
}

ASTIA_TEST(msix_enable_failure_releases_registered_route) {
    reset();
    state.config_write_failure    = true;
    auto                      pci = device();
    astia::module::msix_route route;
    ASTIA_CHECK(route.request(pci, noop_interrupt, nullptr));
    ASTIA_CHECK(route.program());
    ASTIA_CHECK(!route.enable());
    state.config_write_failure = false;
    ASTIA_CHECK(route.release());
    check_released(route);
    ASTIA_CHECK(state.disable_write_count == 1);
}

ASTIA_TEST(msix_unmask_failure_disables_global_msix_before_release) {
    reset();
    state.unmask_failure          = true;
    auto                      pci = device();
    astia::module::msix_route route;
    ASTIA_CHECK(route.request(pci, noop_interrupt, nullptr));
    ASTIA_CHECK(route.program());
    ASTIA_CHECK(route.enable());
    ASTIA_CHECK(state.enable_write_count == 1);
    ASTIA_CHECK(!route.unmask());
    ASTIA_CHECK(route.release());
    check_released(route);
    ASTIA_CHECK(state.disable_write_count == 1);
}

ASTIA_TEST(msix_disable_restore_preserves_disabled_function_mask_state) {
    reset();
    state.control = 0;
    auto pci      = device();
    ASTIA_CHECK(astia::module::pci_disable_msix(pci));
    ASTIA_CHECK(state.control == (1u << 14));
    ASTIA_CHECK(astia::module::pci_restore_msix(pci, 0));
    ASTIA_CHECK(state.control == 0);
}

ASTIA_TEST(msix_rollback_after_bar_map_failure_restores_original_control) {
    reset();
    state.map_failure = true;
    state.control     = 0;
    auto pci          = device();
    ASTIA_CHECK(astia::module::pci_disable_msix(pci));
    ASTIA_CHECK(state.control == (1u << 14));
    ASTIA_CHECK(!astia::module::pci_map_bar(pci, 0));
    ASTIA_CHECK(astia::module::pci_restore_msix(pci, 0));
    ASTIA_CHECK(state.control == 0);
}
