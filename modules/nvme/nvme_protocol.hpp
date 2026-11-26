#pragma once

#include <astia/common.hpp>

#include <stdint.h>

namespace nvme_protocol {
    constexpr uint32_t max_transfer = astia::mm::page_size * 2u;
    constexpr uint8_t  flush_opcode = 0x00;
    constexpr uint8_t  write_opcode = 0x01;
    constexpr uint8_t  read_opcode  = 0x02;

    struct command {
        uint32_t dword[16]{};
    };

    struct geometry {
        bool     started    = false;
        uint64_t blocks     = 0;
        uint32_t block_size = 0;
    };

    enum class request_state : uint8_t { free, pending, canceled, completed };

    inline bool valid_transfer(const geometry& value, const void* buffer, uint32_t length,
                               uint64_t offset) {
        if (!value.started || buffer == nullptr || length == 0 || length > max_transfer ||
            value.block_size == 0 || (length % value.block_size) != 0 ||
            (offset % value.block_size) != 0 ||
            value.blocks > UINT64_MAX / value.block_size)
            return false;

        const uint64_t bytes = value.blocks * value.block_size;

        return offset <= bytes && static_cast<uint64_t>(length) <= bytes - offset;
    }

    inline bool set_prp(command& value, uint64_t physical, uint32_t length) {
        if (physical == UINT64_MAX || length == 0)
            return false;

        value.dword[6] = static_cast<uint32_t>(physical);
        value.dword[7] = static_cast<uint32_t>(physical >> 32);
        value.dword[8] = 0;
        value.dword[9] = 0;

        const uint32_t first =
            astia::mm::page_size -
            static_cast<uint32_t>(physical & (astia::mm::page_size - 1u));

        if (length <= first)
            return true;

        if (length - first > astia::mm::page_size)
            return false;

        const uint64_t page =
            physical & ~(static_cast<uint64_t>(astia::mm::page_size) - 1u);

        if (page > UINT64_MAX - astia::mm::page_size)
            return false;

        const uint64_t second = page + astia::mm::page_size;

        value.dword[8] = static_cast<uint32_t>(second);
        value.dword[9] = static_cast<uint32_t>(second >> 32);

        return true;
    }

    constexpr uint32_t page_count(uint32_t length) {
        return length / astia::mm::page_size +
               (length % astia::mm::page_size != 0 ? 1u : 0u);
    }

    constexpr bool fits_one_page(uint64_t physical, uint32_t length) {
        return physical != UINT64_MAX && length != 0 &&
               length <=
                   astia::mm::page_size -
                       static_cast<uint32_t>(physical & (astia::mm::page_size - 1u));
    }

    constexpr bool valid_queue_depth(uint32_t requested, uint32_t maximum) {
        return requested >= 2 && requested <= maximum;
    }

    inline bool encode_io(command& value, uint8_t opcode, uint16_t cid,
                          uint32_t namespace_id, uint64_t physical, uint32_t length,
                          uint64_t offset, uint32_t block_size, bool fua) {
        if (block_size == 0 || length == 0 || length > max_transfer ||
            (length % block_size) != 0 || (offset % block_size) != 0 ||
            !set_prp(value, physical, length))
            return false;

        value.dword[0]  = opcode | (static_cast<uint32_t>(cid) << 16);
        value.dword[1]  = namespace_id;
        value.dword[10] = static_cast<uint32_t>(offset / block_size);
        value.dword[11] = static_cast<uint32_t>((offset / block_size) >> 32);
        value.dword[12] = (fua ? (1u << 30) : 0) | (length / block_size - 1);

        return true;
    }

    inline void encode_flush(command& value, uint16_t cid, uint32_t namespace_id) {
        value.dword[0] = flush_opcode | (static_cast<uint32_t>(cid) << 16);
        value.dword[1] = namespace_id;
    }

    inline uint16_t completion_cid(uint32_t dword3) {
        return static_cast<uint16_t>(dword3 & 0xffff);
    }

    inline uint16_t completion_status(uint32_t dword3) {
        return static_cast<uint16_t>((dword3 >> 17) & 0x7ff);
    }

    inline bool begin(request_state& state) {
        if (state != request_state::free)
            return false;

        state = request_state::pending;

        return true;
    }

    inline bool cancel(request_state& state) {
        if (state != request_state::pending)
            return false;

        state = request_state::canceled;

        return true;
    }

    inline bool timeout(request_state& state) {
        return cancel(state);
    }

    inline bool complete(request_state& state) {
        if (state != request_state::pending && state != request_state::canceled)
            return false;

        state = request_state::completed;

        return true;
    }

    inline void release(request_state& state) {
        state = request_state::free;
    }
}
