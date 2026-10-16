#include <astia/common.hpp>

namespace astia::core {

    void test_print() {
        asm volatile("mov $0x3F8, %%dx; mov $'T', %%al; outb %%al, %%dx" ::: "dx", "al");
    }

} // namespace astia::core
