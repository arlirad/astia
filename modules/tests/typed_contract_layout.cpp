#include <astia/hw/driver.hpp>
#include <astia/dev/blk/blkdev.hpp>
#include <astia/dev/chr/chrdev.hpp>
#include <astia/io.hpp>
#include <astia/mm/dma_buffer.hpp>

#include <type_traits>

static_assert(std::is_move_constructible_v<astia::mm::dma_buffer>);
static_assert(!std::is_copy_constructible_v<astia::mm::dma_buffer>);
static_assert(sizeof(astia::io::direction) == 1);
static_assert(std::is_base_of_v<astia::dev::device, astia::dev::chrdev>);
static_assert(std::is_base_of_v<astia::dev::device, astia::dev::blk::blkdev>);
static_assert(std::is_polymorphic_v<astia::hw::driver>);

int main() {
    astia::io::request request(nullptr, 0, 0, 0, astia::io::direction::input);
    return request.direction() == astia::io::direction::input ? 0 : 1;
}
