#include <astia/hw/driver.hpp>
#include <astia/dev/blk/blkdev.hpp>
#include <astia/io.hpp>
#include <astia/mm/dma_buffer.hpp>

namespace {

    class fixture_block final : public astia::dev::blk::blkdev {
        public:
        fixture_block() : blkdev("typed", 4096, 512) {}

        protected:
        long read_blocks(void*, astia::u32 length, astia::u64) override {
            return static_cast<long>(length);
        }

        long write_blocks(const void*, astia::u32 length, astia::u64) override {
            return static_cast<long>(length);
        }
    };

    class fixture_driver final : public astia::hw::driver {
        public:
        const char* name() const override { return "typed"; }
        bool match(const astia::hw::device_info&) const override { return false; }
        astia::mm::shptr<astia::dev::device> probe(
                const astia::hw::device_info&) override { return {}; }
    };

    fixture_driver driver;
    fixture_block block;
    astia::mm::dma_buffer buffer;

}

extern "C" int module_main() {
    astia::io::request request(nullptr, 0, 0, 0, astia::io::direction::input);
    (void) request;
    (void) block;
    (void) buffer;
    return astia::hw::drivers().register_driver(driver) ? 0 : -1;
}
