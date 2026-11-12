#pragma once

#include "../device.hpp"

namespace astia::dev::blk {

    class block_device : public device {
        public:
        constexpr block_device(const char* name, u64 size_bytes, u32 block_size)
            : device(name, dv_sync | dv_block), size_bytes_(size_bytes),
              block_size_(block_size) {}

        long read(void* buffer, u32 length, u64 offset) override;
        long write(const void* buffer, u32 length, u64 offset) override;
        long submit(io_request& request) override;

        u64 size_bytes() const {
            return size_bytes_;
        }
        u32 block_size() const {
            return block_size_;
        }

        protected:
        virtual long read_blocks(void* buffer, u32 length, u64 offset) = 0;
        virtual long write_blocks(const void* buffer, u32 length, u64 offset);

        private:
        long validate(const void* buffer, u32 length, u64 offset) const;

        u64 size_bytes_;
        u32 block_size_;
    };

} // namespace astia::dev::blk
