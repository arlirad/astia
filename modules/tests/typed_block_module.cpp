#include <astia/dev/blk/blkdev.hpp>
#include <astia/hw/driver.hpp>
#include <astia/io.hpp>
#include <astia/mm/dma_buffer.hpp>

#include <errno.h>

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

    class fixture_driver final {
        public:
        bool match(const astia::hw::device_info&) const {
            return false;
        }
        astia::i64 probe(const astia::hw::device_info&) {
            return -ENODEV;
        }
        astia::i64 start(const astia::hw::device_info&) {
            return 0;
        }
        void                      stop(const astia::hw::device_info&) {}
        void                      remove(const astia::hw::device_info&) {}
        astia::hw::product_handle publish(const astia::hw::device_info&) {
            return {};
        }
        bool selftest() {
            return true;
        }
    };

    fixture_driver        driver;
    fixture_block         block;
    astia::mm::dma_buffer buffer;

}

int module_main() {
    astia::core::clsource request_source;
    astia::io::request    request(request_source, nullptr, 0, 0, 0,
                                  astia::io::direction::input);
    (void) request;
    (void) block;
    (void) buffer;
    astia::hw::driver_handle handle{};
    const auto               descriptor = astia::hw::make_driver_descriptor(
        driver, "typed", astia::hw::kernel_owner,
        astia::hw::driver_resource_device_file | astia::hw::driver_resource_async);
    return astia::hw::drivers().register_driver(descriptor, handle) == 0 ? 0 : -1;
}
