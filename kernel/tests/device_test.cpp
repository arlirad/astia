#include <astia/api/errno.h>
#include <astia/dev/device.hpp>
#include <astia/dev/device_file.hpp>

#include "test.hpp"

#include <atomic>
#include <cstdlib>
#include <thread>

namespace astia::mm::heap {

    optional<u8*> alloc(usize size) {
        return static_cast<u8*>(std::malloc(size));
    }

    void free(void* value) {
        std::free(value);
    }

}

namespace {
    class submit_only_file final : public astia::fs::file {
        public:
        long submit(astia::io::request& request) override {
            ++calls;
            stable    = request.stable();
            direction = request.direction();
            return static_cast<long>(request.length());
        }

        int                  calls     = 0;
        bool                 stable    = false;
        astia::io::direction direction = astia::io::direction::input;
    };

    class dispatch_file final : public astia::fs::file {
        public:
        long read(void*, astia::u32 length, astia::u64) override {
            ++reads;
            return static_cast<long>(length);
        }
        long write(const void*, astia::u32 length, astia::u64) override {
            ++writes;
            return static_cast<long>(length);
        }
        long submit(astia::io::request&) override {
            ++submits;
            return -EIO;
        }

        int reads   = 0;
        int writes  = 0;
        int submits = 0;
    };

    class test_device final : public astia::dev::device {
        public:
        explicit test_device(const char* name) : device(name, astia::dev::dv_sync) {}

        using device::read;
        using device::write;
        long read(void*, astia::u32, astia::u64) override {
            return 0;
        }
        long write(const void*, astia::u32, astia::u64) override {
            return 0;
        }
    };

    class parked_device final : public astia::dev::device,
                                private astia::io::cancellation_handler {
        public:
        parked_device() : device("parked", astia::dev::dv_mqueue) {}

        long read(void*, astia::u32, astia::u64) override {
            return -EIO;
        }
        long write(const void*, astia::u32, astia::u64) override {
            return -EIO;
        }

        long submit(astia::io::request& request) override {
            if (pending != nullptr)
                return -EBUSY;
            pending = &request;
            request.device_state(this);
            request.cancellation_handler_set(this);
            return 0;
        }

        bool                acknowledge_cancel = true;
        int                 cancel_count       = 0;
        astia::io::request* pending            = nullptr;
        std::atomic<bool>*  cancel_entered     = nullptr;
        std::atomic<bool>*  cancel_gate        = nullptr;

        private:
        void cancellation_requested(astia::io::request& request,
                                    astia::core::cancel_reason, bool retain) override {
            auto* value = static_cast<parked_device*>(request.device_state());
            ++value->cancel_count;
            if (value->cancel_entered != nullptr)
                value->cancel_entered->store(true, std::memory_order_release);
            if (value->cancel_gate != nullptr)
                while (!value->cancel_gate->load(std::memory_order_acquire))
                    std::this_thread::yield();
            value->pending = nullptr;
            request.device_state(nullptr);
            request.cancellation_handler_set(nullptr);
            if (value->acknowledge_cancel || !retain)
                request.cancel_acknowledge();
        }
    };
}

ASTIA_TEST(device_constructor_does_not_probe_past_name_capacity) {
    const char  name[15] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h',
                            'i', 'j', 'k', 'l', 'm', 'n', 'o'};
    test_device device(name);
    ASTIA_CHECK(device.name()[14] == 'o');
    ASTIA_CHECK(device.name()[15] == '\0');
}

ASTIA_TEST(device_span_adapters_reject_u32_overflow) {
    test_device        device("bounded");
    const astia::usize oversized = static_cast<astia::usize>(~astia::u32{0}) + 1;
    ASTIA_CHECK(device.read(astia::span<astia::u8>(nullptr, oversized), 0) == -EINVAL);
    ASTIA_CHECK(device.write(astia::span<const astia::u8>(nullptr, oversized), 0) ==
                -EINVAL);
}

ASTIA_TEST(device_file_detach_rejects_new_operations) {
    test_device             device("detachable");
    astia::dev::device_file file(&device);
    char                    buffer[1]{};
    ASTIA_CHECK(file.read(buffer, sizeof buffer, 0) == 0);
    file.detach();
    ASTIA_CHECK(file.read(buffer, sizeof buffer, 0) == -ENODEV);
    ASTIA_CHECK(file.poll(POLLIN, 0) == POLLNVAL);
}

ASTIA_TEST(device_file_detach_cancels_parked_request_once) {
    parked_device           device;
    astia::dev::device_file file(&device);
    char                    buffer[512]{};
    astia::io::request request(buffer, sizeof buffer, 0, 0, astia::io::direction::input);

    ASTIA_CHECK(file.submit(request) == -EAGAIN);
    ASTIA_CHECK(device.pending == nullptr);
    ASTIA_CHECK(device.cancel_count == 1);
    device.acknowledge_cancel = false;
    file.detach();
    ASTIA_CHECK(device.cancel_count == 1);
    ASTIA_CHECK(device.pending == nullptr);
    ASTIA_CHECK(request.cancellation_requested());
    ASTIA_CHECK(request.cancellation_reason() == astia::core::cancel_reason::request);
    request.cancel_acknowledge();
    ASTIA_CHECK(!request.cancel_device(true));
    ASTIA_CHECK(file.submit(request) == -ENODEV);
}

ASTIA_TEST(device_file_detach_cancels_parked_operation_once) {
    parked_device           device;
    astia::dev::device_file file(&device);
    char                    buffer[1]{};
    astia::io::operation operation(buffer, sizeof buffer, 0, astia::io::direction::input);
    auto&                request = operation.request();

    ASTIA_CHECK(request.park_device());
    ASTIA_CHECK(file.submit(request) == 0);
    ASTIA_CHECK(device.pending == &request);

    file.detach();
    ASTIA_CHECK(device.cancel_count == 1);
    ASTIA_CHECK(device.pending == nullptr);
    ASTIA_CHECK(request.cancellation_requested());
    ASTIA_CHECK(request.cancellation_reason() == astia::core::cancel_reason::detach);

    operation.release_owner();
    ASTIA_CHECK(request.cancellation_requested());
    request.acknowledge();
    ASTIA_CHECK(device.cancel_count == 1);
    ASTIA_CHECK(device.pending == nullptr);
}

ASTIA_TEST(device_file_detach_waits_for_active_cancellation_handler) {
    parked_device           device;
    astia::dev::device_file file(&device);
    char                    buffer[1]{};
    astia::io::operation operation(buffer, sizeof buffer, 0, astia::io::direction::input);
    auto&                request = operation.request();
    std::atomic<bool>    handler_entered{false};
    std::atomic<bool>    allow_ack{false};
    device.cancel_entered = &handler_entered;
    device.cancel_gate    = &allow_ack;

    ASTIA_CHECK(request.park_device());
    ASTIA_CHECK(file.submit(request) == 0);
    ASTIA_CHECK(device.pending == &request);

    std::atomic<bool> detach_done{false};
    std::thread       worker([&] {
        file.detach();
        detach_done.store(true, std::memory_order_release);
    });
    while (!handler_entered.load(std::memory_order_acquire))
        std::this_thread::yield();
    ASTIA_CHECK(!detach_done.load(std::memory_order_acquire));
    allow_ack.store(true, std::memory_order_release);
    worker.join();

    ASTIA_CHECK(detach_done.load(std::memory_order_acquire));
    ASTIA_CHECK(device.cancel_count == 1);
    ASTIA_CHECK(device.pending == nullptr);
    ASTIA_CHECK(request.cancellation_requested());
    request.acknowledge();
    operation.release_owner();
}

ASTIA_TEST(aio_parked_request_stays_stable_across_stretch) {
    char                 first[4]{};
    char                 second[8]{};
    astia::io::operation operation(first, sizeof first, 0x10, 0x1234,
                                   astia::io::direction::input);
    auto&                request = operation.request();

    ASTIA_CHECK(request.stable());
    ASTIA_CHECK(request.park_device());
    const auto  stale_token = request.cancellation_token();
    auto* const address     = &request;
    request.stretch(second, sizeof second, 0x2000);

    ASTIA_CHECK(&request == address);
    ASTIA_CHECK(request.stable());
    ASTIA_CHECK(stale_token.stale());
    ASTIA_CHECK(request.cancellation_token().valid());
    ASTIA_CHECK(request.buffer() == static_cast<void*>(second));
    ASTIA_CHECK(request.length() == sizeof second);
    ASTIA_CHECK(request.offset() == 0x2000);
    ASTIA_CHECK(request.user_data() == 0x1234);
    ASTIA_CHECK(request.direction() == astia::io::direction::input);
    operation.release_owner();
    ASTIA_CHECK(request.cancellation_requested());
    request.acknowledge();
}

ASTIA_TEST(aio_completed_parked_operation_releases_owner_once) {
    char                 buffer[1]{};
    astia::io::operation operation(buffer, sizeof buffer, 0, astia::io::direction::input);
    auto&                request = operation.request();

    ASTIA_CHECK(request.park_device());
    ASTIA_CHECK(request.begin_device());
    request.acknowledge();
    request.end_device();
    operation.release_owner();
}

ASTIA_TEST(open_file_keeps_synchronous_dispatch_fast_path) {
    auto                 object = astia::mm::shptr<astia::fs::file>(new dispatch_file{});
    astia::fs::open_file handle(object, astia::fs::open_file_status_flags::readwrite);
    astia::u8            read_buffer[4]{};
    const astia::u8      write_buffer[2]{};

    ASTIA_CHECK(handle.read(read_buffer, sizeof read_buffer, 0) == sizeof read_buffer);
    ASTIA_CHECK(handle.write(write_buffer, sizeof write_buffer, 0) ==
                sizeof write_buffer);
    auto* file = static_cast<dispatch_file*>(object.get());
    ASTIA_CHECK(file->reads == 1);
    ASTIA_CHECK(file->writes == 1);
    ASTIA_CHECK(file->submits == 0);
}

ASTIA_TEST(open_file_falls_back_to_submit_only_file) {
    auto object = astia::mm::shptr<astia::fs::file>(new submit_only_file{});
    astia::fs::open_file handle(object, astia::fs::open_file_status_flags::readwrite);
    astia::u8            buffer[3]{};

    ASTIA_CHECK(handle.read(buffer, sizeof buffer, 0) == sizeof buffer);
    auto* submitter = static_cast<submit_only_file*>(object.get());
    ASTIA_CHECK(submitter->calls == 1);
    ASTIA_CHECK(submitter->stable);
    ASTIA_CHECK(submitter->direction == astia::io::direction::input);
}
