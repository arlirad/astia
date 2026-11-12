#include "blk/block_device.hpp"
#include "test.hpp"

#include <astia/api/errno.h>
#include <cstring>
#include <vector>

namespace {

    long read_storage(void* context, void* buffer, astia::u32 length, astia::u64 offset) {
        const auto& storage = *static_cast<const std::vector<astia::u8>*>(context);
        if (offset > storage.size() || length > storage.size() - offset)
            return -EINVAL;
        if (length == 0)
            return 0;
        std::memcpy(buffer, storage.data() + offset, length);
        return length;
    }

    class synthetic_block_device final : public astia::dev::blk::block_device {
        public:
        synthetic_block_device()
            : block_device("synthetic", 6, 2), storage_{'a', 's', 't', 'i', 'a', '!'} {}

        std::vector<astia::u8> storage_;

        private:
        long read_blocks(void* buffer, astia::u32 length, astia::u64 offset) override {
            return read_storage(&storage_, buffer, length, offset);
        }
    };

} // namespace

ASTIA_TEST(block_device_reads_at_offset) {
    synthetic_block_device device;
    char buffer[4]{};
    ASTIA_CHECK(device.read(buffer, sizeof buffer, 2) == sizeof buffer);
    ASTIA_CHECK(std::memcmp(buffer, "tia!", sizeof buffer) == 0);
}

ASTIA_TEST(block_device_rejects_invalid_read) {
    synthetic_block_device device;
    ASTIA_CHECK(device.read(nullptr, 0, 6) == 0);
    ASTIA_CHECK(device.read(nullptr, 0, 8) == -EINVAL);
    ASTIA_CHECK(device.read(nullptr, 2, 4) == -EINVAL);
    ASTIA_CHECK(device.read(nullptr, 2, 6) == -EINVAL);
}

ASTIA_TEST(block_device_submit_rejects_unaligned_request) {
    synthetic_block_device device;
    char buffer[2]{};
    astia::io_request request(buffer, sizeof buffer, 1, 0, astia::io_direction::input);
    ASTIA_CHECK(device.submit(request) == -EINVAL);

    request.offset(2);
    ASTIA_CHECK(device.submit(request) == 2);
    ASTIA_CHECK(std::memcmp(buffer, "ti", sizeof buffer) == 0);
}

ASTIA_TEST(block_device_write_is_read_only) {
    synthetic_block_device device;
    const char buffer[2] = {'x', 'y'};
    ASTIA_CHECK(device.write(buffer, sizeof buffer, 0) == -EROFS);
}
