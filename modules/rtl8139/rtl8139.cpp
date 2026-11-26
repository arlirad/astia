#include <astia/module.hpp>

#include "rtl8139_protocol.hpp"

namespace {
    using astia::mm::dma_buffer;
    using astia::module::net_device;
    using astia::rtl8139::bounded_spsc_queue;
    using astia::rtl8139::device_state;
    using astia::rtl8139::packet_view;
    using astia::rtl8139::pci_match;
    using astia::rtl8139::register_file;
    using astia::rtl8139::register_offset;
    using astia::rtl8139::rx_action;
    using astia::rtl8139::rx_deferred_callback;
    using astia::rtl8139::rx_inspect_result;
    using astia::rtl8139::rx_irq_callback;
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
        astia::hw::pci_device*                                           _pci = nullptr;
        register_file                                                    _registers;
        dma_buffer                                                       _rx_ring_dma;
        rx_ring                                                          _rx_ring;
        tx_ring                                                          _tx_ring;
        bounded_spsc_queue<rx_packet, astia::rtl8139::rx_deferred_depth> _rx_deferred;
        astia::module::msix_route                                        _irq_route{};
        net_device        _net_device{"rtl8139", this};
        device_state      _state                 = device_state::detached;
        bool              _irq_active            = false;
        uint64_t          _generation            = 1;
        volatile uint32_t _process_lock          = 0;
        unsigned          _tx_selftest_reclaimed = 0;

        static void tx_selftest_reclaim(void* context, const tx_request*, int status) {
            auto* controller = static_cast<rtl8139_controller*>(context);

            if (controller != nullptr && status == 0)
                ++controller->_tx_selftest_reclaimed;
        }

        void lock_process() {
            uint32_t expected = 0;

            while (!__atomic_compare_exchange_n(&_process_lock, &expected, 1, false,
                                                __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
                expected = 0;

                asm volatile("pause");
            }
        }

        void unlock_process() {
            __atomic_store_n(&_process_lock, 0, __ATOMIC_RELEASE);
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

        bool request_irq() {
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

        void release_irq() {
            if (!_irq_active)
                return;

            astia::module::pci_msix_disable(&_irq_route);
            astia::module::pci_msix_release(&_irq_route);

            _irq_route  = {};
            _irq_active = false;
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
            view.generation  = _net_device.generation();

            rx_action action = _net_device.invoke_rx_irq(view);

            if (action == rx_action::defer) {
                if (_rx_deferred.push(packet))
                    return false;

                action = rx_action::drop;
            }

            if (action == rx_action::recycle || action == rx_action::drop)
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

        int available_tx_slot() const {
            return _tx_ring.available();
        }

        void cancel_all_process() {
            for (uint32_t index = 0; index < astia::rtl8139::tx_descriptor_count;
                 ++index) {
                const tx_request* request = _tx_ring.request(index);
                if (request == nullptr)
                    continue;

                if (_tx_ring.state(index) == tx_slot_state::device)
                    _net_device.tx().abort_inflight(*request, -125);

                _tx_ring.release(index);
            }

            _net_device.tx().reset();

            rx_packet packet{};

            while (_rx_deferred.pop(packet)) {
                recycle_rx(packet);
            }
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

            _net_device.set_mac(mac);
            _net_device.set_mtu(1500);
            _net_device.set_link_flags(astia::net::link_up | astia::net::link_running);
            _net_device.set_generation(_generation);
            _net_device.set_running(true);

            _state = request_irq() ? device_state::running : device_state::probed_no_irq;

            astia::module::log("rtl8139", "probe ok");

            return 0;
        }

        public:
        static void irq_entry(void* context) {
            static_cast<rtl8139_controller*>(context)->interrupt();
        }

        int probe(astia::hw::pci_device* device) {
            return attach(device);
        }

        void set_callbacks(rx_irq_callback immediate, rx_deferred_callback deferred,
                           void* context) {
            _net_device.set_callbacks(immediate, deferred, context);
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
            lock_process();

            unsigned submitted = 0;

            tx_request request{};

            while (available_tx_slot() >= 0 && _net_device.tx().dequeue(request)) {
                const unsigned slot_index = static_cast<unsigned>(available_tx_slot());

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
                ++submitted;
            }

            unlock_process();

            return submitted;
        }

        unsigned poll_completions() {
            lock_process();

            unsigned count = _net_device.tx().poll();

            if (_net_device.running()) {
                rx_packet packet{};

                if (_rx_deferred.pop(packet)) {
                    astia::net::packet_view view{};

                    view.first  = {packet.view.first.data, packet.view.first.length};
                    view.second = {packet.view.second.data, packet.view.second.length};
                    view.length = packet.view.length;
                    view.wire_length = packet.view.wire_length;
                    view.generation  = _net_device.generation();

                    _net_device.invoke_rx_deferred(view);
                    recycle_rx(packet);
                }
            }

            unlock_process();

            return count;
        }

        void interrupt() {
            if (_state != device_state::running)
                return;

            const uint16_t status = _registers.read16(register_offset::interrupt_status);

            if (status == 0 || status == 0xffff)
                return;

            _registers.write16(register_offset::interrupt_status, status);

            if ((status &
                 (astia::rtl8139::interrupt_rx_ok | astia::rtl8139::interrupt_rx_error |
                  astia::rtl8139::interrupt_rx_overflow |
                  astia::rtl8139::interrupt_fifo_overflow)) != 0)
                drain_rx_irq();

            if ((status & (astia::rtl8139::interrupt_tx_ok |
                           astia::rtl8139::interrupt_tx_error)) != 0)
                drain_tx_irq();
        }

        int reset_process() {
            if (_state == device_state::detached)
                return -19;

            lock_process();
            release_irq();
            _registers.write16(register_offset::interrupt_mask, 0);
            _registers.write8(register_offset::command, 0);
            cancel_all_process();
            ++_generation;

            if (_generation == 0)
                _generation = 1;

            _net_device.set_generation(_generation);

            if (!reset_hardware() || !configure_hardware()) {
                _state = device_state::reset;

                _net_device.set_running(false);
                unlock_process();

                return -5;
            }

            _state = request_irq() ? device_state::running : device_state::probed_no_irq;

            _net_device.set_running(true);
            unlock_process();

            return 0;
        }

        int detach_process() {
            if (_state == device_state::detached)
                return 0;

            lock_process();
            release_irq();
            _registers.write16(register_offset::interrupt_mask, 0);
            _registers.write8(register_offset::command, 0);
            cancel_all_process();
            _rx_ring_dma.release();

            _pci       = nullptr;
            _registers = {};

            _net_device.set_running(false);

            _state = device_state::detached;

            unlock_process();

            return 0;
        }

        int selftest() {
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
        astia::net::devices().unregister_device(*controller.device());
        controller.detach_process();
        co_return astia::error_t::enone;
    }

    astia::core::task<bool> selftest() override {
        co_return controller.selftest() != 0;
    }
};

rtl8139_driver driver;

int module_main() {
    astia::hw::driver_handle handle{};

    return astia::hw::drivers().register_driver(driver, handle, astia::hw::kernel_owner,
                                                astia::hw::driver_resource_mmio |
                                                    astia::hw::driver_resource_dma |
                                                    astia::hw::driver_resource_msix |
                                                    astia::hw::driver_resource_async) == 0
               ? 0
               : -22;
}
