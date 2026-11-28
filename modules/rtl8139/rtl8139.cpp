#include <astia/acpi.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/arch/spinlock_guard.hpp>
#include <astia/arch/timer.hpp>
#include <astia/atomic.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/core/task.hpp>
#include <astia/core/timer.hpp>
#include <astia/module.hpp>
#include <astia/net/bus.hpp>
#include <astia/net/registry.hpp>

#include "rtl8139_protocol.hpp"
#include "rtl8139_teardown.hpp"

namespace {
    using astia::mm::dma_buffer;
    using astia::module::net_device;
    using astia::rtl8139::bounded_spsc_queue;
    using astia::rtl8139::device_state;
    using astia::rtl8139::intx_claim;
    using astia::rtl8139::intx_ops;
    using astia::rtl8139::packet_view;
    using astia::rtl8139::pci_match;
    using astia::rtl8139::register_file;
    using astia::rtl8139::register_offset;
    using astia::rtl8139::rx_inspect_result;
    using astia::rtl8139::rx_packet;
    using astia::rtl8139::rx_ring;
    using astia::rtl8139::tx_request;
    using astia::rtl8139::tx_ring;
    using astia::rtl8139::tx_slot_state;

    constexpr uint32_t max_irq_rx_packets = 32;

    register_offset offset_for(uint16_t offset) {
        return static_cast<register_offset>(offset);
    }

    class rtl8139_controller final {
        astia::hw::pci_device*    _pci = nullptr;
        register_file             _registers;
        dma_buffer                _rx_ring_dma;
        rx_ring                   _rx_ring;
        tx_ring                   _tx_ring;
        astia::module::msix_route _irq_route{};
        net_device                _net_device{"rtl8139", this};
        device_state              _state           = device_state::detached;
        static constexpr unsigned poll_interval_ms = 1;

        bool                              _irq_active = false;
        intx_claim                        _intx;
        static inline rtl8139_controller* _intx_owner = nullptr;
        bool                              _polling    = false;
        uint64_t                          _generation = 1;
        astia::core::spinlock             _process{};
        unsigned                          _tx_selftest_reclaimed = 0;
        uint32_t                          _tx_next               = 0;

        static void tx_selftest_reclaim(void* context, const tx_request*, int status) {
            auto* controller = static_cast<rtl8139_controller*>(context);

            if (controller != nullptr && status == 0)
                ++controller->_tx_selftest_reclaimed;
        }

        uint8_t* rx_buffer() const {
            return _rx_ring_dma ? static_cast<uint8_t*>(_rx_ring_dma.data()) : nullptr;
        }

        bool physical_address_ok(uint64_t physical) const {
            return physical != UINT64_MAX && physical <= UINT32_MAX;
        }

        bool reset_hardware() {
            _registers.write8(register_offset::command, astia::rtl8139::command_reset);

            for (uint32_t spin = 0; spin < 1000000; ++spin) {
                if ((_registers.read8(register_offset::command) &
                     astia::rtl8139::command_reset) == 0)
                    return true;

                asm volatile("pause");
            }

            return false;
        }

        bool configure_hardware() {
            if (!physical_address_ok(_rx_ring_dma.physical()))
                return false;

            _registers.write32(register_offset::rx_buffer_start,
                               static_cast<uint32_t>(_rx_ring_dma.physical()));
            _registers.write32(register_offset::rx_config, (3u << 8) | 0x0fu);
            _registers.write32(register_offset::tx_config, 6u << 8);
            _registers.write8(register_offset::command,
                              astia::rtl8139::command_receiver_enable |
                                  astia::rtl8139::command_transmitter_enable);
            _registers.write16(register_offset::interrupt_status, 0xffffu);
            _registers.write16(register_offset::interrupt_mask,
                               astia::rtl8139::interrupt_rx_tx);
            _registers.write16(register_offset::current_address,
                               astia::rtl8139::capr_for_cursor(_rx_ring.cursor()));

            return true;
        }

        bool request_intx() {
            if (_pci == nullptr || _pci->interrupt_pin == 0)
                return false;

            astia::u8 line = _pci->interrupt_line;
            astia::u8 gsi  = 0;

            if (line == 0 || line == 0xff) {
                astia::acpi::pci_route route{};

                if (!astia::acpi::resolve_pci_route(_pci->bus, _pci->device,
                                                    _pci->function, _pci->interrupt_pin,
                                                    route))
                    return false;

                line = route.irq;
                gsi  = route.gsi;
            }
            else {
                if (line > 15)
                    return false;

                gsi = astia::acpi::gsi_for_irq(line);
            }

            if (line > 15)
                return false;

            if (!astia::arch::register_irq_handler(line, &rtl8139_controller::intx_entry,
                                                   &rtl8139_controller::intx_enter,
                                                   &rtl8139_controller::intx_leave, this))
                return false;

            if (!_intx.claim(line))
                return false;

            const uint16_t command = astia::module::pci_read16(*_pci, 0x04);

            (void) astia::module::pci_write16(
                *_pci, 0x04, static_cast<uint16_t>(command & ~(1u << 10)));

            const astia::u64 base = astia::acpi::ioapic_base();

            if (!astia::arch::route_gsi(line, base, gsi) ||
                !astia::arch::unmask_irq_handler(line)) {
                if (!_intx.release(intx_operations()))
                    return false;

                return false;
            }

            _intx_owner = this;

            return true;
        }

        static bool intx_enter(void* context) {
            const auto* self = static_cast<const rtl8139_controller*>(context);

            return self != nullptr && self->_state != device_state::detached;
        }

        static void intx_leave(void*) {}

        static void report_intx_deferred(astia::u8 line) {
            astia::module::log_u64("rtl8139", "intx unregister deferred", line);
        }

        static const intx_ops& intx_operations() {
            static constexpr intx_ops ops{&astia::arch::unregister_irq_handler,
                                          &astia::arch::mask_irq_handler,
                                          &rtl8139_controller::report_intx_deferred};

            return ops;
        }

        static void intx_entry() {
            if (_intx_owner != nullptr)
                _intx_owner->interrupt();
        }

        bool request_irq() {
            if (request_intx())
                return true;

            return request_msix();
        }

        bool request_msix() {
            if (_pci == nullptr || !_pci->interrupt.msix)
                return false;

            if (!astia::module::pci_msix_request(_pci, irq_entry, this, &_irq_route))
                return false;

            if (!astia::module::pci_msix_program(&_irq_route) ||
                !astia::module::pci_msix_enable(&_irq_route) ||
                !astia::module::pci_msix_unmask(&_irq_route)) {
                astia::module::pci_msix_release(&_irq_route);

                _irq_route = {};

                return false;
            }

            _irq_active = true;

            return true;
        }

        void stop_polling() {
            if (!_polling)
                return;

            (void) astia::core::detail::disarm_timer(this);

            _polling = false;
        }

        void start_polling() {
            if (_polling || _state == device_state::detached)
                return;

            const astia::u32 hz = astia::arch::timer_hz();
            if (hz == 0)
                return;

            astia::u64 ticks =
                (static_cast<astia::u64>(poll_interval_ms) * hz + 999) / 1000;
            if (ticks == 0)
                ticks = 1;

            if (astia::core::detail::arm_timer_callback(
                    this, &rtl8139_controller::poll_entry, ticks))
                _polling = true;
        }

        static void poll_entry(void* context) {
            auto* self = static_cast<rtl8139_controller*>(context);

            self->_polling = false; // the one-shot timer has fired

            self->service_polled();
            self->start_polling();
        }

        bool release_irq() {
            stop_polling();

            if (_intx.active()) {
                if (!_intx.release(intx_operations()))
                    return false;

                _intx_owner = nullptr;
            }

            if (!_irq_active)
                return true;

            astia::module::pci_msix_disable(&_irq_route);
            astia::module::pci_msix_release(&_irq_route);

            _irq_route  = {};
            _irq_active = false;

            return true;
        }

        void recycle_rx(uint16_t next) {
            if (_rx_ring.recycle_to(next))
                _registers.write16(register_offset::current_address,
                                   astia::rtl8139::capr_for_cursor(_rx_ring.cursor()));
        }

        void recycle_rx(const rx_packet& packet) {
            if (_rx_ring.recycle(packet))
                _registers.write16(register_offset::current_address,
                                   astia::rtl8139::capr_for_cursor(_rx_ring.cursor()));
        }

        bool inspect_and_dispatch(rx_packet& packet) {
            const rx_inspect_result result = _rx_ring.acquire(packet);

            if (result == rx_inspect_result::empty)
                return false;

            if (result == rx_inspect_result::malformed) {
                const uint16_t wire_length = static_cast<uint16_t>(
                    astia::rtl8139::ring_byte(rx_buffer(), _rx_ring.cursor() + 2) |
                    static_cast<uint16_t>(
                        astia::rtl8139::ring_byte(rx_buffer(), _rx_ring.cursor() + 3)
                        << 8));

                if (wire_length >= astia::rtl8139::rx_crc_size &&
                    wire_length <= astia::rtl8139::rx_max_frame_with_crc) {
                    recycle_rx(astia::rtl8139::ring_offset(
                        static_cast<uint32_t>(_rx_ring.cursor()) +
                        astia::rtl8139::rx_header_size + wire_length + 3u));

                    return true;
                }

                return false;
            }

            astia::net::packet_view view{};

            view.first       = {packet.view.first.data, packet.view.first.length};
            view.second      = {packet.view.second.data, packet.view.second.length};
            view.length      = packet.view.length;
            view.wire_length = packet.view.wire_length;

            _net_device.dispatch_rx(view);

            recycle_rx(packet);

            return true;
        }

        void drain_rx_irq() {
            const uint16_t hardware_cursor =
                static_cast<uint16_t>(_registers.read16(register_offset::current_buffer) &
                                      (astia::rtl8139::rx_ring_size - 1u));
            rx_packet packet{};

            for (uint32_t count = 0;
                 count < max_irq_rx_packets && _rx_ring.pending(hardware_cursor);
                 ++count) {
                if (!inspect_and_dispatch(packet))
                    break;
            }
        }

        void drain_tx_irq() {
            astia::arch::interrupt_spinlock_guard guard(_process);

            for (uint32_t index = 0; index < astia::rtl8139::tx_descriptor_count;
                 ++index) {
                if (_tx_ring.state(index) != tx_slot_state::device)
                    continue;

                const uint32_t status = _registers.read32(offset_for(
                    static_cast<uint16_t>(register_offset::tx_status0) + index * 4u));

                if ((status & astia::rtl8139::tx_status_own) == 0)
                    continue;

                tx_request completed{};

                if (!_tx_ring.complete(index, completed))
                    continue;

                const int result = (status & astia::rtl8139::tx_status_ok) != 0 ? 0 : -5;

                if (_net_device.tx().complete(completed, result))
                    _tx_ring.release(index);
                else
                    _tx_ring.restore(index);
            }
        }

        /// Called with `_process` held: stops the transmitter and aborts
        /// every descriptor the device may still be DMA-ing from.
        void quiesce_tx_locked() {
            _registers.write8(register_offset::command,
                              _registers.read8(register_offset::command) &
                                  ~astia::rtl8139::command_transmitter_enable);

            for (uint32_t index = 0; index < astia::rtl8139::tx_descriptor_count;
                 ++index) {
                const tx_request* request = _tx_ring.request(index);
                if (request == nullptr)
                    continue;

                if (_tx_ring.state(index) == tx_slot_state::device)
                    _net_device.tx().abort_inflight(*request, -125);

                _tx_ring.release(index);
            }
        }

        static void quiesce_tx(void* context) {
            auto* self = static_cast<rtl8139_controller*>(context);

            astia::arch::interrupt_spinlock_guard guard(self->_process);

            if (self->_state == device_state::detached)
                return;

            self->quiesce_tx_locked();
        }

        int attach(astia::hw::pci_device* device) {
            if (!pci_match(device) || _state != device_state::detached)
                return -19;

            uint8_t bar = 0xff;

            for (uint8_t index = 0; index < device->bar_count && index < 6; ++index) {
                if (device->bars[index].present && !device->bars[index].io) {
                    bar = index;

                    break;
                }
            }

            if (bar == 0xff || !astia::module::pci_enable_bus_master(device) ||
                !astia::module::pci_map_bar(device, bar))
                return -19;

            const uint64_t mapped = astia::module::pci_bar_mapped(device, bar);
            const uint64_t size   = astia::module::pci_bar_size(device, bar);

            if (mapped == 0 || size < 0x100)
                return -19;

            _pci       = device;
            _registers = register_file(reinterpret_cast<volatile uint8_t*>(mapped), size);

            if (!_rx_ring_dma.allocate(astia::rtl8139::rx_ring_pages, true) ||
                !reset_hardware() || !configure_hardware()) {
                _rx_ring_dma.release();

                _pci       = nullptr;
                _registers = {};

                return -12;
            }

            _rx_ring.reset(rx_buffer());

            uint8_t mac[6]{};

            for (unsigned i = 0; i < 6; ++i) {
                mac[i] = _registers.read8(offset_for(i));
            }

            _net_device.set_kind(astia::net::link_kind::ethernet);
            _net_device.set_mac(mac);
            _net_device.set_mtu(1500);
            _net_device.set_link_flags(astia::net::link_up);
            _net_device.set_generation(_generation);
            _net_device.tx().set_quiesce(&rtl8139_controller::quiesce_tx, this);
            _net_device.tx().set_kick(&rtl8139_controller::kick_tx, this);

            _state = request_irq() ? device_state::running : device_state::probed_no_irq;

            if (_state == device_state::probed_no_irq) {
                _registers.write16(register_offset::interrupt_mask, 0);
                astia::module::log("rtl8139", "polling rx fallback");
                start_polling();
            }

            astia::module::log("rtl8139", "probe ok");

            return 0;
        }

        public:
        static void irq_entry(void* context) {
            static_cast<rtl8139_controller*>(context)->interrupt();
        }

        static void kick_tx(void* context) {
            (void) static_cast<rtl8139_controller*>(context)->service_tx();
        }

        int probe(astia::hw::pci_device* device) {
            return attach(device);
        }

        int submit(const tx_request& request) {
            if ((_state != device_state::running &&
                 _state != device_state::probed_no_irq) ||
                request.data == nullptr || request.physical > UINT32_MAX ||
                request.length == 0 || request.length > astia::rtl8139::tx_max_frame ||
                request.reclaim == nullptr)
                return -22;

            return _net_device.tx().submit(request);
        }

        unsigned service_tx() {
            astia::arch::interrupt_spinlock_guard guard(_process);

            unsigned submitted = 0;

            tx_request request{};

            // The device starts the descriptor at its own cursor and advances
            // on success; writing descriptors out of order stalls the queue.
            while (_tx_ring.state(_tx_next) == tx_slot_state::free &&
                   _net_device.tx().dequeue(request)) {
                const unsigned slot_index = _tx_next;

                if (!_tx_ring.claim(slot_index, request))
                    break;

                _registers.write32(
                    offset_for(static_cast<uint16_t>(register_offset::tx_address0) +
                               slot_index * 4u),
                    request.physical);
                _registers.write32(
                    offset_for(static_cast<uint16_t>(register_offset::tx_status0) +
                               slot_index * 4u),
                    request.length);

                _tx_next = (_tx_next + 1u) % astia::rtl8139::tx_descriptor_count;

                ++submitted;
            }

            return submitted;
        }

        unsigned poll_completions() {
            astia::arch::interrupt_spinlock_guard guard(_process);

            return _net_device.tx().poll();
        }

        /// Legacy/no-MSI path: the chip raises no usable ISR bits, so service
        /// RX and TX from the poll tick directly.
        void service_polled() {
            if (_state == device_state::detached)
                return;

            drain_rx_irq();
            drain_tx_irq();
            (void) service_tx();
            (void) poll_completions();
        }

        void interrupt() {
            if (_state != device_state::running && _state != device_state::probed_no_irq)
                return;

            const uint16_t status = _registers.read16(register_offset::interrupt_status);

            if (status != 0 && status != 0xffff)
                _registers.write16(register_offset::interrupt_status, status);

            // Drain from the ring cursors, not only from the status bits: a
            // packet can arrive between the status read and the ack write,
            // and its bit would be cleared without being serviced.
            drain_rx_irq();
            drain_tx_irq();
            (void) service_tx();
            (void) poll_completions();
        }

        int reset_process() {
            if (_state == device_state::detached)
                return -19;

            if (!release_irq())
                return -16;

            _net_device.tx().set_kick(nullptr, nullptr);
            _net_device.tx().reset();

            {
                astia::arch::interrupt_spinlock_guard guard(_process);

                _registers.write16(register_offset::interrupt_mask, 0);
                _registers.write8(register_offset::command, 0);

                ++_generation;

                if (_generation == 0)
                    _generation = 1;

                _net_device.set_generation(_generation);

                if (!reset_hardware() || !configure_hardware()) {
                    _state = device_state::reset;

                    return -5;
                }

                _state =
                    request_irq() ? device_state::running : device_state::probed_no_irq;

                if (_state == device_state::probed_no_irq) {
                    _registers.write16(register_offset::interrupt_mask, 0);
                    start_polling();
                }

                _net_device.tx().set_quiesce(&rtl8139_controller::quiesce_tx, this);
                _net_device.tx().set_kick(&rtl8139_controller::kick_tx, this);
            }

            return 0;
        }

        int detach_process() {
            if (_state == device_state::detached)
                return 0;

            if (!release_irq())
                return -16;

            _net_device.tx().set_kick(nullptr, nullptr);
            _net_device.tx().reset();

            {
                astia::arch::interrupt_spinlock_guard guard(_process);

                _registers.write16(register_offset::interrupt_mask, 0);
                _registers.write8(register_offset::command, 0);
                _rx_ring_dma.release();

                _pci       = nullptr;
                _registers = {};
                _tx_next   = 0;

                _state = device_state::detached;
            }

            return 0;
        }

        int selftest() {
            // The selftest drives the public queue by hand; the production
            // kick would dequeue and program the device behind its back.
            struct kick_guard {
                rtl8139_controller* self;

                ~kick_guard() {
                    self->_net_device.tx().set_kick(&rtl8139_controller::kick_tx, self);
                }
            } guard{this};

            _net_device.tx().set_kick(nullptr, nullptr);

            uint8_t    byte = 0;
            tx_request request{};

            request.data    = &byte;
            request.length  = 1;
            request.reclaim = tx_selftest_reclaim;

            const int submitted = _net_device.tx().submit(request);
            if (submitted != 0) {
                astia::module::log_u64("rtl8139", "public tx submit failed", submitted);

                return 0;
            }

            tx_request active{};

            if (!_net_device.tx().dequeue(active)) {
                astia::module::log("rtl8139", "public tx dequeue failed");

                return 0;
            }

            if (!_net_device.tx().complete(active, 0)) {
                astia::module::log("rtl8139", "public tx complete failed");

                return 0;
            }

            if (_net_device.tx().poll() != 1 || _tx_selftest_reclaimed != 1) {
                astia::module::log("rtl8139", "public tx reclaim failed");

                return 0;
            }

            astia::module::log("rtl8139", "public tx selftest ok");

            return 1;
        }

        void initialize() {
            _net_device.initialize("rtl8139", this);
            _rx_ring.initialize();
            _tx_ring.initialize();

            _tx_next = 0;
        }

        net_device* device() {
            return &_net_device;
        }
    };

    rtl8139_controller controller;
} // namespace

class rtl8139_driver final : public astia::hw::driver {
    public:
    const char* name() const override {
        return "rtl8139";
    }

    bool match(const astia::hw::device_info& info) const override {
        const auto* pci = astia::hw::pci_device_info::get(info);
        return pci != nullptr && astia::rtl8139::pci_match(&pci->device());
    }

    astia::core::task<astia::i64> probe(const astia::hw::device_info& info) override {
        const auto* pci = astia::hw::pci_device_info::get(info);
        if (pci == nullptr)
            co_return -ENODEV;
        auto* stable = astia::hw::pci_devices().find(pci->device().bdf);
        if (stable == nullptr || controller.probe(stable) != 0)
            co_return -ENODEV;
        if (!astia::net::devices().register_device(*controller.device())) {
            controller.detach_process();

            co_return -ENODEV;
        }

        (void) co_await astia::net::bus::bind(*controller.device());

        co_return 0;
    }

    astia::core::task<astia::i64> start(const astia::hw::device_info&) override {
        co_return 0;
    }

    astia::core::task<void> stop(const astia::hw::device_info&) override {
        co_return astia::error_t::enone;
    }

    astia::hw::product_handle publish(const astia::hw::device_info&) override {
        return {};
    }

    astia::core::task<void> remove(const astia::hw::device_info&) override {
        if (controller.detach_process() != 0) {
            astia::module::log("rtl8139", "detach deferred: irq dispatch in flight");

            co_return astia::error_t::ebusy;
        }

        (void) co_await astia::net::bus::unbind(*controller.device());
        astia::net::devices().unregister_device(*controller.device());
        co_return astia::error_t::enone;
    }

    astia::core::task<bool> selftest() override {
        co_return controller.selftest() != 0;
    }
};

rtl8139_driver driver;

astia::core::task<void> module_main() {
    astia::hw::driver_handle handle{};

    co_return astia::hw::drivers().register_driver(
        driver, handle, astia::hw::kernel_owner,
        astia::hw::driver_resource_mmio | astia::hw::driver_resource_dma |
            astia::hw::driver_resource_msix | astia::hw::driver_resource_async) == 0
        ? astia::error_t::enone
        : astia::error_t::eio;
}
