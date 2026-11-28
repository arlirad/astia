#include <astia/api/errno.h>

#include "nvme_test_support.hpp"

#define ASTIA_TEARDOWN_TEST 1
#include "../nvme.cpp"
}

namespace {
    bool woken = false;

    void mark_woken(void*) {
        woken = true;
    }

    bool released(const void* address) {
        for (unsigned i = 0; i < nvme_test::released_dma_count; ++i) {
            if (nvme_test::released_dma[i] == address)
                return true;
        }

        return false;
    }

    struct fixture {
        explicit fixture(unsigned char* registers) {
            nvme_test::host_dma = true;

            device.started   = true;
            device.registers = registers;
            space.owner      = &device;
            space.id         = 1;
            space.size       = 4;
            space.block_size = 512;

            assert(device.io_sq.allocate());
            assert(device.io_cq.allocate());
        }

        ~fixture() {
            nvme_test::host_dma = false;
        }

        controller       device;
        namespace_device space;
        char             buffer[512]{};
    };

    void assert_timeout_completes_once() {
        alignas(8) unsigned char registers[register_space + 0x40]{};

        fixture f(registers);

        astia::core::clsource request_source;
        astia::io::request    request(request_source, f.buffer, sizeof f.buffer, 0, 0,
                                      astia::io::direction::output);

        assert(f.device.submit_async(f.space, request) == 0);
        assert(f.device.async_requests[0].used);

        const void* bounce = &f.device.async_requests[0].bounce;

        assert(!released(bounce));

        woken = false;

        request.device_state(&f.device);
        request.wake_handler_set(mark_woken, nullptr);
        request.timeout_expired(true);

        assert(f.device.poll_async(request) == 0);
        assert(woken);
        assert(request.timeout_expired());

        long result = 0;

        assert(f.device.complete_async(request, result));
        assert(result == -ETIMEDOUT);
        assert(!f.device.async_requests[0].used);
        assert(released(bounce));
        assert(!f.device.complete_async(request, result));

        assert(f.device.poll_async(request) == 0);
    }

    void assert_normal_completion_reports_length() {
        alignas(8) unsigned char registers[register_space + 0x40]{};

        fixture f(registers);

        astia::core::clsource request_source;
        astia::io::request    request(request_source, f.buffer, sizeof f.buffer, 0, 0,
                                      astia::io::direction::output);

        assert(f.device.submit_async(f.space, request) == 0);

        const unsigned short cid = f.device.async_requests[0].cid;

        auto* entries = static_cast<completion*>(f.device.io_cq.data());

        entries[0].dword[3] = static_cast<unsigned>(cid) | (1u << 16);

        assert(f.device.poll_async(request) == 0);
        assert(!request.timeout_expired());

        long result = 0;

        assert(f.device.complete_async(request, result));
        assert(result == static_cast<long>(sizeof f.buffer));
        assert(!f.device.async_requests[0].used);
        assert(f.device.io_cq_head == 1);
        assert(!f.device.complete_async(request, result));
    }
}

int main() {
    assert_timeout_completes_once();
    assert_normal_completion_reports_length();

    return 0;
}
