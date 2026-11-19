#include "../virtio_blk_protocol.hpp"
#include <astia/common.hpp>

#include <cassert>
#include <cstdint>

int main() {
    using namespace astia_virtio_blk;
    static_assert(astia::mm::page_size == astia::mm::page_size);
    assert(max_transfer == astia::mm::page_size * 2u);
    assert(page_count(0) == 0);
    assert(page_count(astia::mm::page_size) == 1);
    assert(page_count(astia::mm::page_size + 1u) == 2);
    uint8_t header[16]{};
    encode_header(header, request_out, 7);
    assert(header[0] == 1 && header[1] == 0 && header[8] == 7 && header[9] == 0);
    encode_header(header, request_flush, 0);
    assert(header[0] == 4 && header[8] == 0);
    assert(data_flags(false) == descriptor_next);
    assert(data_flags(true) == (descriptor_next | descriptor_write));
    assert(valid_range(8, 512, 0));
    assert(valid_range(8, 512, 7 * 512));
    assert(!valid_range(8, 513, 0));
    assert(!valid_range(8, 512, 8 * 512));
    assert(safe_write(feature_flush));
    assert(!safe_write(feature_read_only | feature_flush));
    assert(!safe_write(0));
    assert(completion_length_ok(request_in, 512, 513));
    assert(!completion_length_ok(request_in, 512, 1));
    assert(completion_length_ok(request_out, 512, 1));
    assert(completion_length_ok(request_flush, 0, 1));
    assert(request_state::submitted != request_state::canceled);
    return 0;
}
