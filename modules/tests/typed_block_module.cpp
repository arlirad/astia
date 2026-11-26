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

    class fixture_driver final : public astia::hw::driver {
        public:
        const char* name() const override {
            return "typed";
        }

        bool match(const astia::hw::device_info&) const override {
            return false;
        }

        astia::core::task<astia::i64> probe(const astia::hw::device_info&) override {
            co_return -ENODEV;
        }

        astia::core::task<astia::i64> start(const astia::hw::device_info&) override {
            co_return 0;
        }

        astia::core::task<void> stop(const astia::hw::device_info&) override {
            co_return astia::error_t::enone;
        }

        astia::core::task<void> remove(const astia::hw::device_info&) override {
            co_return astia::error_t::enone;
        }

        astia::hw::product_handle publish(const astia::hw::device_info&) override {
            return {};
        }

        bool selftest() override {
            return true;
        }
    };

    fixture_driver        driver;
    fixture_block         block;
    astia::mm::dma_buffer buffer;
}

int module_main() {
    astia::hw::driver_handle handle{};

    return astia::hw::drivers().register_driver(driver, handle, astia::hw::kernel_owner,
                                                astia::hw::driver_resource_device_file |
                                                    astia::hw::driver_resource_async) == 0
               ? 0
               : -1;
}
