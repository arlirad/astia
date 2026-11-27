#include "nvme_test_support.hpp"

#define ASTIA_TEARDOWN_TEST 1
#include "../nvme.cpp"
}

namespace {
    void reset() {
        nvme_test::bar_mapped          = 0;
        nvme_test::bar_size            = register_space;
        nvme_test::bus_master_enables  = 0;
        nvme_test::bus_master_disables = 0;
        nvme_test::bar_unmaps          = 0;
        nvme_test::msix_disables       = 0;
        nvme_test::map_bar_result      = true;
        nvme_test::unmap_bar_result    = true;
        nvme_test::disable_msix_result = true;
    }

    void assert_rejected(astia::hw::pci_device& pci, const controller& value) {
        assert(value.registers == nullptr);
        assert(!value.started);
        assert(!pci.bars[0].mapped_valid);
        assert(!pci.bus_master_enabled);
        assert(nvme_test::bar_unmaps == 1);
        assert(nvme_test::bus_master_disables == 1);
        assert(nvme_test::msix_disables == 0);
    }
}

int main() {
    {
        reset();

        astia::hw::pci_device pci{};
        controller            value{};

        value.pci = &pci;

        assert(!value.start());
        assert_rejected(pci, value);
    }

    {
        reset();

        astia::hw::pci_device pci{};
        controller            value{};

        nvme_test::bar_size = 0;
        value.pci           = &pci;

        assert(!value.start());
        assert_rejected(pci, value);
    }

    {
        reset();

        astia::hw::pci_device pci{};
        controller            value{};

        nvme_test::bar_mapped = 0x100000;
        nvme_test::bar_size   = register_space - 1;
        value.pci             = &pci;

        assert(!value.start());
        assert_rejected(pci, value);
    }

    return 0;
}
