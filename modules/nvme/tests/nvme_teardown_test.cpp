#include <astia/io.hpp>
#include <astia/mm/dma_buffer.hpp>

#include <cassert>

namespace {
    unsigned    dma_release_count = 0;
    const void* released_dma[64]{};
    unsigned    released_dma_count = 0;
}

namespace astia::mm {
    dma_buffer::dma_buffer(dma_buffer&&) noexcept {}

    dma_buffer& dma_buffer::operator=(dma_buffer&&) noexcept {
        return *this;
    }

    dma_buffer::~dma_buffer() {
        release();
    }

    bool dma_buffer::allocate(usz, bool) {
        return false;
    }

    void dma_buffer::release() {
        for (unsigned i = 0; i < released_dma_count; ++i) {
            if (released_dma[i] == this)
                return;
        }

        assert(released_dma_count < sizeof released_dma / sizeof released_dma[0]);

        released_dma[released_dma_count++] = this;

        ++dma_release_count;
    }

    void dma_buffer::swap(dma_buffer&) noexcept {}
}

#define ASTIA_TEARDOWN_TEST 1
#include "../nvme.cpp"
}

namespace astia::module {
    msix_route::~msix_route() {}

    bool pci_msix_release(msix_route*) {
        return true;
    }
}

namespace {}

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

    const unsigned before_release = dma_release_count;

    value.release();
    assert(dma_release_count == before_release);

    long result = 0;

    assert(value.complete_async(request, result));
    assert(result == -19);
    assert(!value.complete_async(request, result));

    value.release();
    assert(dma_release_count == before_release + 7);

    {
        controller                               unsafe{};
        alignas(unsigned) volatile unsigned char registers[0x20]{};

        registers[0x14]                  = 1;
        registers[0x1c]                  = 2;
        unsafe.registers                 = registers;
        unsafe.started                   = true;
        unsafe.async_requests[0].used    = true;
        unsafe.async_requests[0].request = nullptr;

        const unsigned before_unsafe = dma_release_count;

        unsafe.quiesce();
        unsafe.release();
        assert(dma_release_count == before_unsafe);
    }

    return 0;
}
