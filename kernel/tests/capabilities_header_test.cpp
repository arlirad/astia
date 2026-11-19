#include <astia/dev/capabilities.hpp>

int main() {
    astia::dev::pio_bus  pio;
    astia::dev::io_port  port{pio, 0};
    astia::dev::irq_line irq;
    return port.valid() || irq.valid();
}
