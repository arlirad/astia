#include <astia/api/errno.h>
#include <astia/core/resume_queue.hpp>
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

    class dispatch_file final : public astia::fs::file,
                                public astia::fs::readable,
                                public astia::fs::writable {
        public:
        astia::optional<astia::fs::readable&> as_readable() override {
            return *this;
        }

        astia::optional<astia::fs::writable&> as_writable() override {
            return *this;
        }

        astia::core::task<astia::i64> readv(astia::span<const astia::fs::buffer> buffers,
                                            astia::u64                           offset,
                                            astia::core::cltoken token) override {
            return submit_readv(buffers, offset, token);
        }

        astia::core::task<astia::i64> writev(astia::span<const astia::fs::buffer> buffers,
                                             astia::u64                           offset,
                                             astia::core::cltoken                 token,
                                             bool nonblock) override {
            return submit_writev(buffers, offset, token, nonblock);
        }

        long submit(astia::io::request& request) override {
            ++submits;
            return static_cast<long>(request.length());
        }

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

    class parked_device final : public astia::dev::device {
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
            return 0;
        }

        astia::io::request* pending = nullptr;
    };

    class queue_device final : public astia::dev::device {
        public:
        queue_device() : device("queue", astia::dev::dv_mqueue) {}

        long read(void*, astia::u32, astia::u64) override {
            return -EIO;
        }

        long write(const void*, astia::u32, astia::u64) override {
            return -EIO;
        }

        long submit(astia::io::request& request) override {
            if (!ready)
                return 0;
            if (request.buffer() != nullptr)
                __builtin_memset(request.buffer(), 0x5a, request.length());
            return static_cast<long>(request.length());
        }

        bool ready = false;
    };

    class blocking_device final : public astia::dev::device {
        public:
        blocking_device() : device("blocking", astia::dev::dv_sync) {}

        std::atomic<bool> entered{false};
        std::atomic<bool> allow{false};

        long read(void*, astia::u32, astia::u64) override {
            return -EIO;
        }

        long write(const void*, astia::u32, astia::u64) override {
            return -EIO;
        }

        long submit(astia::io::request&) override {
            entered.store(true, std::memory_order_release);
            while (!allow.load(std::memory_order_acquire))
                std::this_thread::yield();
            return -EIO;
        }
    };

    void mark_quiesced(void* context, astia::u64) {
        static_cast<std::atomic<bool>*>(context)->store(true, std::memory_order_release);
    }
}

ASTIA_TEST(device_constructor_does_not_probe_past_name_capacity) {
    const char name[15] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h',
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

ASTIA_TEST(device_file_services_a_parked_completion) {
    queue_device device;

    astia::dev::device_file file(&device);

    astia::u8                     buffer[4]{};
    const astia::fs::buffer       entry{buffer, sizeof buffer};
    astia::core::task<astia::i64> task = file.readv(
        astia::span<const astia::fs::buffer>(&entry, 1), 0, astia::core::cltoken{});

    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(!task.done());
    ASTIA_CHECK(!file.pending().empty());

    device.ready = true;

    file.service_ready();
    astia::core::drain_resumes();
    ASTIA_CHECK(task.done());
    ASTIA_CHECK(!task.failed());
    ASTIA_CHECK(task.value() != nullptr && *task.value() == 4);
    ASTIA_CHECK(buffer[0] == 0x5a);
}

ASTIA_TEST(device_file_detach_rejects_new_operations) {
    test_device device("detachable");

    astia::dev::device_file file(&device);

    auto                 parent = astia::mm::shptr<astia::fs::file>::borrowed(&file);
    astia::fs::open_file handle(parent, astia::fs::open_file_status_flags::readwrite);

    char buffer[1]{};

    ASTIA_CHECK(handle.read(buffer, sizeof buffer, 0) == 0);
    file.detach();
    ASTIA_CHECK(handle.read(buffer, sizeof buffer, 0) == -ENODEV);
    ASTIA_CHECK(file.poll(POLLIN, 0) == POLLNVAL);
}

ASTIA_TEST(device_file_detach_cancels_parked_request_once) {
    parked_device device;

    astia::dev::device_file file(&device);

    char buffer[512]{};

    astia::core::clsource request_source;
    astia::io::request    request(request_source, buffer, sizeof buffer, 0, 0,
                                  astia::io::direction::input);

    ASTIA_CHECK(file.submit(request) == -EAGAIN);
    file.detach();
    ASTIA_CHECK(request.cancellation_requested());
    ASTIA_CHECK(request.cancellation_reason() == astia::core::cancel_reason::request);
    ASTIA_CHECK(!request.cancel_device(true));
    ASTIA_CHECK(file.submit(request) == -ENODEV);
}

ASTIA_TEST(device_file_detach_cancels_parked_operation_once) {
    parked_device device;

    astia::dev::device_file file(&device);

    char buffer[1]{};

    astia::io::operation operation(buffer, sizeof buffer, 0, astia::io::direction::input);

    auto& request = operation.request();

    ASTIA_CHECK(request.park_device());
    ASTIA_CHECK(file.submit(request) == 0);
    ASTIA_CHECK(device.pending == &request);

    file.detach();
    ASTIA_CHECK(request.cancellation_requested());
    ASTIA_CHECK(request.cancellation_reason() == astia::core::cancel_reason::detach);

    operation.release_owner();
    ASTIA_CHECK(request.cancellation_requested());
    request.acknowledge();
}

ASTIA_TEST(device_file_quiescence_callback_waits_for_active_submit) {
    blocking_device device;

    astia::dev::device_file file(&device);

    char buffer[1]{};

    astia::core::clsource request_source;
    astia::io::request    request(request_source, buffer, sizeof buffer, 0, 0,
                                  astia::io::direction::input);
    std::atomic<bool>     submit_done{false};
    std::atomic<bool>     quiesced{false};

    std::thread submitter([&] {
        file.submit(request);
        submit_done.store(true, std::memory_order_release);
    });

    while (!device.entered.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    file.detach();
    ASTIA_CHECK(!file.quiescent());
    ASTIA_CHECK(!file.arm_quiescence_handler(mark_quiesced, &quiesced, 1));
    ASTIA_CHECK(!quiesced.load(std::memory_order_acquire));

    device.allow.store(true, std::memory_order_release);
    submitter.join();

    while (!quiesced.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    ASTIA_CHECK(submit_done.load(std::memory_order_acquire));
    ASTIA_CHECK(file.quiescent());
}

ASTIA_TEST(aio_parked_request_stays_stable_across_stretch) {
    char first[4]{};
    char second[8]{};

    astia::io::operation operation(first, sizeof first, 0x10, 0x1234,
                                   astia::io::direction::input);

    auto& request = operation.request();

    ASTIA_CHECK(request.stable());
    ASTIA_CHECK(request.park_device());

    const auto  stale_token = request.cancellation_token();
    auto* const address     = &request;

    request.stretch(second, sizeof second, 0x2000);

    ASTIA_CHECK(&request == address);
    ASTIA_CHECK(request.stable());
    ASTIA_CHECK(!stale_token.stale());
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
    char buffer[1]{};

    astia::io::operation operation(buffer, sizeof buffer, 0, astia::io::direction::input);

    auto& request = operation.request();

    ASTIA_CHECK(request.park_device());
    ASTIA_CHECK(request.begin_device());
    request.acknowledge();
    request.end_device();
    operation.release_owner();
}

ASTIA_TEST(file_reports_only_the_contracts_it_implements) {
    dispatch_file    dispatch;
    submit_only_file submit_only;

    ASTIA_CHECK(dispatch.as_readable());
    ASTIA_CHECK(dispatch.as_writable());
    ASTIA_CHECK(!dispatch.as_pollable());
    ASTIA_CHECK(!submit_only.as_readable());
    ASTIA_CHECK(!submit_only.as_writable());
    ASTIA_CHECK(!submit_only.as_pollable());
}

ASTIA_TEST(open_file_read_and_write_use_the_submit_engine) {
    auto                 parent = astia::mm::shptr<astia::fs::file>(new dispatch_file{});
    astia::fs::open_file handle(parent, astia::fs::open_file_status_flags::readwrite);
    astia::u8            read_buffer[4]{};
    const astia::u8      write_buffer[2]{};

    ASTIA_CHECK(handle.read(read_buffer, sizeof read_buffer, 0) == sizeof read_buffer);
    ASTIA_CHECK(handle.write(write_buffer, sizeof write_buffer, 0) ==
                sizeof write_buffer);

    auto* file = static_cast<dispatch_file*>(parent.get());

    ASTIA_CHECK(file->submits == 2);
}

ASTIA_TEST(open_file_falls_back_to_submit_only_file) {
    auto parent = astia::mm::shptr<astia::fs::file>(new submit_only_file{});
    astia::fs::open_file handle(parent, astia::fs::open_file_status_flags::readwrite);
    astia::u8            buffer[3]{};

    ASTIA_CHECK(handle.read(buffer, sizeof buffer, 0) == sizeof buffer);

    auto* submitter = static_cast<submit_only_file*>(parent.get());

    ASTIA_CHECK(submitter->calls == 1);
    ASTIA_CHECK(submitter->stable);
    ASTIA_CHECK(submitter->direction == astia::io::direction::input);
}
