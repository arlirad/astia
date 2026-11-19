#include <astia/mm/dma_buffer.hpp>
#include <astia/io.hpp>

#include <cassert>

namespace {
    unsigned dma_release_count = 0;
    const void* released_dma[64]{};
    unsigned released_dma_count = 0;
}

namespace astia::mm {

    dma_buffer::dma_buffer(dma_buffer&&) noexcept {}
    dma_buffer& dma_buffer::operator=(dma_buffer&&) noexcept { return *this; }
    dma_buffer::~dma_buffer() { release(); }
    bool dma_buffer::allocate(usize, bool) { return false; }
    void dma_buffer::release() {
        for (unsigned i = 0; i < released_dma_count; ++i)
            if (released_dma[i] == this)
                return;
        assert(released_dma_count < sizeof released_dma / sizeof released_dma[0]);
        released_dma[released_dma_count++] = this;
        ++dma_release_count;
    }
    void dma_buffer::swap(dma_buffer&) noexcept {}

}

#define ASTIA_TEARDOWN_TEST 1
#include "../nvme.cpp"
}

extern "C" int astia_module_pci_msix_release(astia_module_msix_route*) {
    return 0;
}

namespace {

    void retain_cancel(astia::io::request&) {}

    void count_ack(astia::io::request&, void* context) {
        ++*static_cast<unsigned*>(context);
    }

}

int main() {
    controller value{};
    astia::io::request request(nullptr, 512, 0, 0, astia::io::direction::input);
    unsigned acknowledgements = 0;
    request.cancel_observer_set(count_ack, &acknowledgements);
    request.cancel_handler_set(retain_cancel);
    auto& pending = value.async_requests[0];
    pending.used = true;
    pending.request = &request;

    assert(request.cancel_device(true));
    value.quiesce();
    const unsigned before_release = dma_release_count;
    value.release();
    assert(dma_release_count == before_release);
    assert(acknowledgements == 0);

    long result = 0;
    assert(value.complete_async(request, result));
    assert(result == -19);
    assert(acknowledgements == 1);
    assert(!value.complete_async(request, result));

    value.release();
    assert(dma_release_count == before_release + 7);
    request.cancel_acknowledge();
    assert(acknowledgements == 1);
    return 0;
}
