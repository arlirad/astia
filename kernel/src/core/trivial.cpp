#include <astia/common.hpp>

namespace astia::core {

    extern "C" void trivial_cpp() {
        asm volatile("mov $0x3F8, %%dx; mov $'T', %%al; outb %%al, %%dx" ::: "dx", "al");
    }

    void trivial() {
        asm volatile("mov $0x3F8, %%dx; mov $'M', %%al; outb %%al, %%dx" ::: "dx", "al");
    }

} // namespace astia::core
