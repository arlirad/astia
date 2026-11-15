#include "../nvme_protocol.hpp"
#include <astia/common.hpp>

#include <cassert>
#include <cstdint>

int main() {
    using namespace nvme_protocol;

    geometry geometry{true, 16, 512};
    static_assert(ASTIA_PAGE_SIZE == astia::mm::page_size);
    char buffer[max_transfer]{};
    assert(valid_transfer(geometry, buffer, 512, 0));
    assert(valid_transfer(geometry, buffer, max_transfer, 0));
    assert(!valid_transfer(geometry, buffer, 513, 0));
    assert(!valid_transfer(geometry, buffer, 512, 1));
    assert(!valid_transfer(geometry, buffer, 512, max_transfer));
    assert(!valid_transfer(geometry, buffer, 1024, max_transfer - 512));
    geometry.started = false;
    assert(!valid_transfer(geometry, buffer, 512, 0));
    geometry = {true, UINT64_MAX, UINT32_MAX};
    assert(!valid_transfer(geometry, buffer, 512, 0));

    command write{};
    assert(encode_io(write, write_opcode, 7, 1, 0x2000, 1024, 1024, 512, true));
    assert((write.dword[0] & 0xff) == write_opcode);
    assert((write.dword[0] >> 16) == 7);
    assert(write.dword[1] == 1);
    assert(write.dword[10] == 2);
    assert(write.dword[12] == ((1u << 30) | 1u));
    assert(!encode_io(write, write_opcode, 7, 1, 0x2000, 12288, 0, 512, true));

    command read{};
    assert(encode_io(read, read_opcode, 8, 1, 0x2fff, 4097, 0, 1, false));
    assert(read.dword[8] == 0x3000);
    assert(read.dword[9] == 0);
    assert(!set_prp(read, ASTIA_PAGE_SIZE - 1u, max_transfer));
    assert(set_prp(read, ASTIA_PAGE_SIZE, max_transfer));
    assert(read.dword[8] == 0x2000);
    assert(set_prp(read, ASTIA_PAGE_SIZE - 1u, ASTIA_PAGE_SIZE + 1u));
    assert(!set_prp(read, ASTIA_PAGE_SIZE - 1u, ASTIA_PAGE_SIZE + 2u));
    assert(!set_prp(read, 0, 0));
    assert(page_count(0) == 0);
    assert(page_count(ASTIA_PAGE_SIZE) == 1);
    assert(page_count(ASTIA_PAGE_SIZE + 1u) == 2);
    assert(page_count(max_transfer) == 2);
    assert(fits_one_page(ASTIA_PAGE_SIZE, ASTIA_PAGE_SIZE));
    assert(fits_one_page(ASTIA_PAGE_SIZE - 1u, 1));
    assert(!fits_one_page(ASTIA_PAGE_SIZE - 1u, 2));
    assert(!fits_one_page(UINT64_MAX, 1));

    command flush{};
    encode_flush(flush, 9, 1);
    assert((flush.dword[0] & 0xff) == flush_opcode);
    assert((flush.dword[0] >> 16) == 9);
    assert(flush.dword[1] == 1);

    const uint32_t completion = (7u) | (3u << 17);
    assert(completion_cid(completion) == 7);
    assert(completion_status(completion) == 3);

    request_state state = request_state::free;
    assert(begin(state));
    assert(!begin(state));
    assert(cancel(state));
    assert(!cancel(state));
    assert(complete(state));
    assert(!complete(state));
    release(state);
    assert(state == request_state::free);
    assert(begin(state));
    assert(timeout(state));
    assert(complete(state));
    release(state);
}
