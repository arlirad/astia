#include <astia/dev/irq.hpp>

int main() {
    astia::dev::irq_line irq;
    return irq.valid();
}
