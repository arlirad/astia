#include <astia/dev/blk/blkdev.hpp>
#include <astia/dev/chr/chrdev.hpp>

#include <type_traits>

static_assert(std::is_base_of_v<astia::dev::device, astia::dev::blk::blkdev>);
static_assert(std::is_base_of_v<astia::dev::device, astia::dev::chrdev>);

int main() {
    return 0;
}
