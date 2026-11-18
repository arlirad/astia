#include <astia/api/errno.h>
#include <astia/dev/device.hpp>
#include <astia/dev/device_file.hpp>
#include "test.hpp"

namespace {
    class test_device final : public astia::dev::device {
        public:
        explicit test_device(const char* name) : device(name, astia::dev::dv_sync) {}

        using device::read;
        using device::write;
        long read(void*, astia::u32, astia::u64) override { return 0; }
        long write(const void*, astia::u32, astia::u64) override { return 0; }
    };
}

ASTIA_TEST(device_constructor_does_not_probe_past_name_capacity) {
    const char name[15] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h',
                           'i', 'j', 'k', 'l', 'm', 'n', 'o'};
    test_device device(name);
    ASTIA_CHECK(device.name()[14] == 'o');
    ASTIA_CHECK(device.name()[15] == '\0');
}

ASTIA_TEST(device_span_adapters_reject_u32_overflow) {
    test_device device("bounded");
    const astia::usize oversized = static_cast<astia::usize>(~astia::u32{0}) + 1;
    ASTIA_CHECK(device.read(astia::span<astia::u8>(nullptr, oversized), 0) == -EINVAL);
    ASTIA_CHECK(device.write(astia::span<const astia::u8>(nullptr, oversized), 0) == -EINVAL);
}

ASTIA_TEST(device_file_detach_rejects_new_operations) {
    test_device device("detachable");
    astia::dev::device_file file(&device);
    char buffer[1]{};
    ASTIA_CHECK(file.read(buffer, sizeof buffer, 0) == 0);
    file.detach();
    ASTIA_CHECK(file.read(buffer, sizeof buffer, 0) == -ENODEV);
    ASTIA_CHECK(file.poll(POLLIN, 0) == POLLNVAL);
}
