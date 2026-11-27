#include "nvme_test_support.hpp"

#define ASTIA_TEARDOWN_TEST 1
#include "../nvme.cpp"
}

int main() {
    controller            value{};
    astia::core::clsource request_source;
    astia::io::request    request(request_source, nullptr, 512, 0, 0,
                                  astia::io::direction::input);

    auto& pending = value.async_requests[0];

    pending.used    = true;
    pending.request = &request;

    assert(request.cancel_device(true));
    value.quiesce();

    const unsigned before_release = nvme_test::dma_release_count;

    value.release();
    assert(nvme_test::dma_release_count == before_release);

    long result = 0;

    assert(value.complete_async(request, result));
    assert(result == -19);
    assert(!value.complete_async(request, result));

    value.release();
    assert(nvme_test::dma_release_count == before_release + 7);

    {
        controller                               unsafe{};
        alignas(unsigned) volatile unsigned char registers[0x20]{};

        registers[0x14]                  = 1;
        registers[0x1c]                  = 2;
        unsafe.registers                 = registers;
        unsafe.started                   = true;
        unsafe.async_requests[0].used    = true;
        unsafe.async_requests[0].request = nullptr;

        const unsigned before_unsafe = nvme_test::dma_release_count;

        unsafe.quiesce();
        unsafe.release();
        assert(nvme_test::dma_release_count == before_unsafe);
    }

    return 0;
}
