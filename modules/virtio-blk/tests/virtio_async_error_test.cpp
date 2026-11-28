#include <astia/io.hpp>
#include <astia/mm/dma_buffer.hpp>

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {
    unsigned dma_allocations = 0;
    unsigned dma_frees       = 0;
    void*    abandoned[8]{};
    unsigned abandoned_count = 0;

    uint16_t used_slots = 0;
}

namespace astia::mm {
    dma_buffer::dma_buffer(dma_buffer&& other) noexcept {
        swap(other);
    }

    dma_buffer& dma_buffer::operator=(dma_buffer&& other) noexcept {
        if (this != &other)
            swap(other);

        return *this;
    }

    dma_buffer::~dma_buffer() {
        release();
    }

    bool dma_buffer::allocate(usz pages, bool zero) {
        void* memory = std::aligned_alloc(page_size, pages * page_size);
        if (memory == nullptr)
            return false;

        if (zero)
            std::memset(memory, 0, pages * page_size);

        _address  = memory;
        _physical = reinterpret_cast<u64>(memory);
        _pages    = pages;

        ++dma_allocations;

        return true;
    }

    void dma_buffer::abandon() {
        assert(abandoned_count < sizeof abandoned / sizeof abandoned[0]);

        abandoned[abandoned_count++] = _address;

        _address  = nullptr;
        _physical = 0;
        _pages    = 0;
    }

    void dma_buffer::release() {
        if (_address == nullptr)
            return;

        std::free(_address);

        _address  = nullptr;
        _physical = 0;
        _pages    = 0;

        ++dma_frees;
    }

    void dma_buffer::swap(dma_buffer& other) noexcept {
        const u64 physical = _physical;

        void* const address = _address;

        const usz pages = _pages;

        _physical       = other._physical;
        _address        = other._address;
        _pages          = other._pages;
        other._physical = physical;
        other._address  = address;
        other._pages    = pages;
    }
}

#define ASTIA_TEARDOWN_TEST 1
#include "../virtio_blk.cpp"
}

namespace {
    struct fake_device {
        alignas(uint32_t) volatile unsigned char common[0x40]{};
        alignas(uint32_t) volatile unsigned char notify[0x40]{};
    };

    void configure(controller& value, fake_device& fake) {
        value.started           = true;
        value.capacity          = 2048;
        value.flush_supported   = true;
        value.write_supported   = true;
        value.ring_reset        = false;
        value.notify_multiplier = 1;
        value.common            = mmio{fake.common};
        value.notify            = mmio{fake.notify};
        value.notify_cap.length = sizeof fake.notify;

        value._requestqueue.size              = 8;
        value._requestqueue.descriptor_offset = 0;
        value._requestqueue.available_offset  = 8 * 16;
        value._requestqueue.used_offset =
            (value._requestqueue.available_offset + 6 + 8 * 2 + 3) & ~3u;

        assert(value._requestqueue.memory.allocate(1));
        value._requestqueue.clear();

        value._requestqueue.enabled       = true;
        value._requestqueue.notify_offset = 0;

        fake.common[0x14] =
            status_driver_ok | status_driver | status_acknowledge | status_features_ok;
    }

    void post_used(controller& value, uint32_t id, uint32_t length) {
        auto* used = const_cast<used_ring*>(value._requestqueue.used());

        const unsigned slot = used->index % value._requestqueue.size;

        used->ring[slot].id     = id;
        used->ring[slot].length = length;
        used->index             = static_cast<uint16_t>(used->index + 1);

        ++used_slots;
    }

    void post_status(controller& value, uint8_t status) {
        static_cast<unsigned char*>(
            value.pending.request.data())[value.pending.status_offset] = status;
    }

    void wake_counting(astia::io::request& request, unsigned& count) {
        request.wake_handler_set(
            [](void* context) { ++*static_cast<unsigned*>(context); }, &count);
    }

    void assert_completed_once(controller& value, astia::io::request& request,
                               unsigned& wakes) {
        long result = 0;

        assert(!value.complete_async(request, result));
        assert(!value.pending.active);
        assert(value.poll_async(request) == -2);
        assert(wakes == 1);
    }

    void test_status_error(uint8_t status, long expected) {
        const unsigned frees = dma_frees;

        controller  value{};
        fake_device fake{};

        configure(value, fake);

        alignas(8) unsigned char buffer[512]{};

        astia::io::request request(buffer, sizeof buffer, 0, 0,
                                   astia::io::direction::input);

        unsigned wakes = 0;

        wake_counting(request, wakes);

        assert(value.submit_async(request) == 0);

        request.device_state(&value);

        post_status(value, status);
        post_used(value, 0, 512 + 1);

        assert(value.resume_async(request) == expected);
        assert(request.device_state() == nullptr);
        assert(value._requestqueue.used_index == 1);
        assert(dma_frees == frees + 1);

        assert_completed_once(value, request, wakes);

        value.release();
    }

    void test_invalid_used_element() {
        const unsigned frees = dma_frees;

        controller  value{};
        fake_device fake{};

        configure(value, fake);

        alignas(8) unsigned char buffer[512]{};

        astia::io::request request(buffer, sizeof buffer, 0, 0,
                                   astia::io::direction::input);

        unsigned wakes = 0;

        wake_counting(request, wakes);

        assert(value.submit_async(request) == 0);

        request.device_state(&value);

        post_used(value, 1, 512 + 1);

        assert(value.resume_async(request) == -5);
        assert(value._requestqueue.used_index == 1);
        assert(dma_frees == frees + 1);

        assert_completed_once(value, request, wakes);

        value.release();
    }

    void test_write_error() {
        const unsigned frees = dma_frees;

        controller  value{};
        fake_device fake{};

        configure(value, fake);

        alignas(8) unsigned char buffer[512]{};

        std::memset(buffer, 0xa5, sizeof buffer);

        astia::io::request request(buffer, sizeof buffer, 0, 0,
                                   astia::io::direction::output);

        unsigned wakes = 0;

        wake_counting(request, wakes);

        assert(value.submit_async(request) == 0);

        request.device_state(&value);

        post_status(value, status_unsupp);
        post_used(value, 0, 512 + 1);

        assert(value.resume_async(request) == -95);
        assert(value._requestqueue.used_index == 1);
        assert(dma_frees == frees + 1);

        assert_completed_once(value, request, wakes);

        value.release();
    }

    void test_reset_mid_flight() {
        const unsigned frees = dma_frees;

        controller  value{};
        fake_device fake{};

        configure(value, fake);

        alignas(8) unsigned char buffer[512]{};

        astia::io::request request(buffer, sizeof buffer, 0, 0,
                                   astia::io::direction::input);

        unsigned wakes = 0;

        wake_counting(request, wakes);

        assert(value.submit_async(request) == 0);

        request.device_state(&value);

        post_used(value, 0, 512 + 1);

        fake.common[0x14] = status_needs_reset;

        assert(value.resume_async(request) == -5);
        assert(request.device_state() == nullptr);
        assert(value._requestqueue.used_index == 1);
        assert(value.failed);
        assert(dma_frees == frees + 1);

        assert_completed_once(value, request, wakes);

        value.release();
    }

    void test_timeout_without_reset() {
        const unsigned frees = dma_frees;

        controller  value{};
        fake_device fake{};

        configure(value, fake);

        alignas(8) unsigned char buffer[512]{};

        astia::io::request request(buffer, sizeof buffer, 0, 0,
                                   astia::io::direction::input);

        unsigned wakes = 0;

        wake_counting(request, wakes);

        assert(value.submit_async(request) == 0);

        request.device_state(&value);
        request.timeout_expired(true);

        assert(value.resume_async(request) == -110);
        assert(request.device_state() == nullptr);
        assert(value.failed);
        assert(dma_frees == frees);
        assert(abandoned_count == 1);

        assert_completed_once(value, request, wakes);

        value.release();
    }
}

int main() {
    test_status_error(1, -5);
    test_status_error(status_unsupp, -95);
    test_invalid_used_element();
    test_write_error();
    test_reset_mid_flight();
    test_timeout_without_reset();

    assert(dma_allocations == dma_frees + abandoned_count);
    assert(used_slots == 5);

    for (unsigned i = 0; i < abandoned_count; ++i) {
        std::free(abandoned[i]);
    }

    return 0;
}
