#include "blk/block_device.hpp"
#include "test.hpp"

#include <cstring>
#include <vector>

namespace {

    bool read_storage(void* context, astia::u64 offset, void* buffer, astia::usize length) {
        const auto& storage = *static_cast<const std::vector<astia::u8>*>(context);
        if (offset > storage.size() || length > storage.size() - offset)
            return false;
        if (length == 0)
            return true;
        std::memcpy(buffer, storage.data() + offset, length);
        return true;
    }

    class synthetic_block_device final {
        public:
        synthetic_block_device() : storage_{'a', 's', 't', 'i', 'a', '!'}, device{
            &ops, &storage_, storage_.size(), 512} {}

        std::vector<astia::u8> storage_;
        astia::dev::blk::block_device device;

        private:
        static constexpr astia::dev::blk::block_device_ops ops{
            astia::dev::blk::block_device_abi_version, read_storage};
    };

} // namespace

ASTIA_TEST(block_device_reads_at_offset) {
    synthetic_block_device device;
    char buffer[4]{};
    ASTIA_CHECK(astia::dev::blk::read_at(device.device, 1, buffer, sizeof buffer));
    ASTIA_CHECK(std::memcmp(buffer, "stia", sizeof buffer) == 0);
}

ASTIA_TEST(block_device_rejects_invalid_read) {
    synthetic_block_device device;
    ASTIA_CHECK(!astia::dev::blk::read_at(device.device, 0, nullptr, 1));
    ASTIA_CHECK(astia::dev::blk::read_at(device.device, 6, nullptr, 0));
    ASTIA_CHECK(!astia::dev::blk::read_at(device.device, 5, nullptr, 2));
    ASTIA_CHECK(!astia::dev::blk::read_at(device.device, 7, nullptr, 0));
}
