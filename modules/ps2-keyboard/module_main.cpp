#include <astia/dev/capabilities.hpp>
#include <astia/dev/tty_input.hpp>
#include <astia/log.hpp>
#include <astia/module.hpp>

#include "ps2_keyboard.hpp"

namespace {
    using logs = astia::log::context<"ps2-keyboard">;

    using astia::ps2::input_sink;
    using astia::ps2::ps2_controller;
    using astia::ps2::ps2_error;
    using astia::ps2::ps2_keyboard;

    bool ttv0_put(void*, astia::u8 byte) {
        return astia::dev::tty_input_write(byte);
    }

    constexpr input_sink ttv0_sink{nullptr, ttv0_put};

    constinit ps2_controller controller{};
    constinit ps2_keyboard   keyboard{controller, ttv0_sink};

    // The module owns the device identity: line 1 and the 8042 port pair.
    constinit astia::dev::irq_route route{1};

    const char* error_text(ps2_error error) {
        switch (error) {
        case ps2_error::none:
            return "ok";
        case ps2_error::invalid:
            return "invalid";
        case ps2_error::timeout:
            return "timeout";
        case ps2_error::protocol:
            return "protocol";
        case ps2_error::self_test:
            return "self-test";
        case ps2_error::port_test:
            return "port-test";
        }

        return "unknown";
    }

    int error_code(ps2_error error) {
        return -32 - static_cast<int>(error);
    }

    void irq_entry() {
        keyboard.irq();
    }

    void shutdown() {
        (void) controller.disable_interrupt();
        (void) controller.disable_keyboard();
    }

    int fail(ps2_error error) {
        shutdown();
        logs::info("unavailable");
        logs::info("%s", error_text(error));

        return error_code(error);
    }
} // namespace

int module_main() {
    logs::info("resident module loaded");

    const astia::dev::io_port_window& ports = astia::dev::platform_input_window();

    if (!ports.valid()) {
        logs::info("unavailable");

        return -19;
    }

    controller.attach(ports);

    ps2_error result = controller.initialize();

    if (result != ps2_error::none)
        return fail(result);

    result = keyboard.initialize();

    if (result != ps2_error::none)
        return fail(result);

    result = controller.drain();

    if (result != ps2_error::none)
        return fail(result);

    // PIC IRQ1 is still masked; enable the controller source before any
    // route can point at this image.
    result = controller.enable_interrupt();

    if (result != ps2_error::none)
        return fail(result);

    // The kernel owns the module reference behind this route and marks the
    // caller resident itself; there is no module-side lifetime object.
    if (!route.claim(irq_entry)) {
        shutdown();
        logs::info("unavailable");
        logs::info("irq claim failed");

        return -16;
    }

    if (!astia::module::make_resident()) {
        (void) route.release();
        shutdown();
        logs::info("unavailable");
        logs::info("resident transition failed");

        return -21;
    }

    logs::info("ready irq=1");

    // Unmask last: the lease, route, and controller source are all in place.
    if (!route.unmask()) {
        (void) route.release();
        shutdown();
        logs::info("unavailable");
        logs::info("irq unmask failed");

        return -5;
    }

    return 0;
}
