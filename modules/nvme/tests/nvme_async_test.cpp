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

    void complete_entry(controller& device, unsigned index, unsigned short cid,
                        unsigned status) {
        auto* entries = static_cast<completion*>(device.io_cq.data());

        entries[index].dword[3] =
            static_cast<unsigned>(cid) | (1u << 16) | (status << 17);
    }

    void assert_async_completion_first_stays_with_async() {
        alignas(8) unsigned char registers[register_space + 0x40]{};

        fixture f(registers);

        astia::core::clsource request_source;
        astia::io::request    request(request_source, f.buffer, sizeof f.buffer, 0, 0,
                                      astia::io::direction::input);

        assert(f.device.submit_async(f.space, request) == 0);

        const unsigned short async_cid = f.device.async_requests[0].cid;
        unsigned short       sync_cid  = 0;

        assert(f.device.begin_sync(sync_cid));

        command own{};

        own.dword[0] = io_read | (static_cast<unsigned>(sync_cid) << 16);

        f.device.submit_io(own);

        complete_entry(f.device, 0, async_cid, 5);
        complete_entry(f.device, 1, sync_cid, 0);

        assert(f.device.poll_io(sync_cid) == 0);
        assert(f.device.io_cq_head == 2);
        assert(f.device.async_requests[0].completed);
        assert(f.device.async_requests[0].hardware_done);

        long result = 0;

        assert(f.device.complete_async(request, result));
        assert(result == -5);
        assert(!f.device.async_requests[0].used);
    }

    void assert_sync_completion_first_stays_with_sync() {
        alignas(8) unsigned char registers[register_space + 0x40]{};

        fixture f(registers);

        astia::core::clsource request_source;
        astia::io::request    request(request_source, f.buffer, sizeof f.buffer, 0, 0,
                                      astia::io::direction::input);

        assert(f.device.submit_async(f.space, request) == 0);

        const unsigned short async_cid = f.device.async_requests[0].cid;
        unsigned short       sync_cid  = 0;

        assert(f.device.begin_sync(sync_cid));

        command own{};

        own.dword[0] = io_read | (static_cast<unsigned>(sync_cid) << 16);

        f.device.submit_io(own);

        complete_entry(f.device, 0, sync_cid, 0);
        complete_entry(f.device, 1, async_cid, 0);

        assert(f.device.poll_io(sync_cid) == 0);
        assert(f.device.io_cq_head == 1);
        assert(!f.device.async_requests[0].completed);

        f.device.service_async();

        assert(f.device.io_cq_head == 2);
        assert(f.device.async_requests[0].completed);

        long result = 0;

        assert(f.device.complete_async(request, result));
        assert(result == static_cast<long>(sizeof f.buffer));
        assert(!f.device.async_requests[0].used);
    }

    void assert_async_service_hands_sync_completion_back() {
        alignas(8) unsigned char registers[register_space + 0x40]{};

        fixture f(registers);

        unsigned short sync_cid = 0;

        assert(f.device.begin_sync(sync_cid));

        command own{};

        own.dword[0] = io_read | (static_cast<unsigned>(sync_cid) << 16);

        f.device.submit_io(own);

        complete_entry(f.device, 0, sync_cid, 3);

        f.device.service_async();

        assert(f.device.io_cq_head == 1);
        assert(f.device.poll_io(sync_cid) == 3);
        assert(f.device.io_cq_head == 1);
    }

    void assert_unknown_completion_is_discarded() {
        alignas(8) unsigned char registers[register_space + 0x40]{};

        fixture f(registers);

        unsigned short sync_cid = 0;

        assert(f.device.begin_sync(sync_cid));

        complete_entry(f.device, 0, 0x7ffe, 7);
        complete_entry(f.device, 1, sync_cid, 4);

        assert(f.device.poll_io(sync_cid) == 4);
        assert(f.device.io_cq_head == 2);
    }

    void assert_late_async_completion_does_not_stall_queue() {
        alignas(8) unsigned char registers[register_space + 0x40]{};

        fixture f(registers);

        astia::core::clsource first_source;
        astia::io::request    first(first_source, f.buffer, sizeof f.buffer, 0, 0,
                                    astia::io::direction::output);

        assert(f.device.submit_async(f.space, first) == 0);

        const unsigned short late_cid = f.device.async_requests[0].cid;
        long                 result   = 0;

        first.timeout_expired(true);

        assert(f.device.poll_async(first) == 0);
        assert(f.device.complete_async(first, result));
        assert(result == -ETIMEDOUT);

        astia::core::clsource second_source;
        astia::io::request    second(second_source, f.buffer, sizeof f.buffer, 0, 0,
                                     astia::io::direction::output);

        assert(f.device.submit_async(f.space, second) == 0);

        const unsigned short live_cid = f.device.async_requests[0].cid;

        complete_entry(f.device, 0, late_cid, 0);
        complete_entry(f.device, 1, live_cid, 0);

        f.device.service_async();

        assert(f.device.io_cq_head == 2);
        assert(f.device.async_requests[0].completed);
        assert(f.device.complete_async(second, result));
        assert(result == static_cast<long>(sizeof f.buffer));
    }

    void assert_reset_does_not_reuse_pending_cid() {
        alignas(8) unsigned char registers[register_space + 0x40]{};

        fixture f(registers);

        astia::core::clsource request_source;
        astia::io::request    request(request_source, f.buffer, sizeof f.buffer, 0, 0,
                                      astia::io::direction::input);

        assert(f.device.submit_async(f.space, request) == 0);

        const unsigned short async_cid = f.device.async_requests[0].cid;
        unsigned short       sync_cid  = 0;

        f.device.reset_queue_state();

        assert(f.device.begin_sync(sync_cid));
        assert(sync_cid != async_cid);

        complete_entry(f.device, 0, async_cid, 0);
        complete_entry(f.device, 1, sync_cid, 7);

        assert(f.device.poll_io(sync_cid) == 7);
        assert(f.device.io_cq_head == 2);
        assert(f.device.async_requests[0].completed);

        long result = 0;

        assert(f.device.complete_async(request, result));
        assert(result == static_cast<long>(sizeof f.buffer));
    }
}

int main() {
    assert_timeout_completes_once();
    assert_normal_completion_reports_length();
    assert_async_completion_first_stays_with_async();
    assert_sync_completion_first_stays_with_sync();
    assert_async_service_hands_sync_completion_back();
    assert_unknown_completion_is_discarded();
    assert_late_async_completion_does_not_stall_queue();
    assert_reset_does_not_reuse_pending_cid();

    return 0;
}
