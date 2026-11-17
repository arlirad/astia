#include <astia/module_abi.hpp>

#include "rtl8139_protocol.hpp"

namespace {

using astia::module::dma_buffer;
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
using astia::rtl8139::tx_request;

constexpr uint32_t max_irq_rx_packets = 32;

register_offset offset_for(uint16_t offset) {
    return static_cast<register_offset>(offset);
}

struct tx_slot {
    bool active = false;
    bool completion_queued = false;
    int completion_status = 0;
    tx_request request{};
};

class rtl8139_controller final {
    astia_module_pci_device* pci_ = nullptr;
    register_file registers_;
    dma_buffer rx_ring_dma_;
    uint16_t rx_cursor_ = 0;
    tx_slot tx_slots_[astia::rtl8139::tx_descriptor_count]{};
    bounded_spsc_queue<rx_packet, astia::rtl8139::rx_deferred_depth> rx_deferred_;
    astia_module_msix_route irq_route_{};
    net_device net_device_{"rtl8139", this};
    device_state state_ = device_state::detached;
    bool irq_active_ = false;
    uint64_t generation_ = 1;
    volatile uint32_t process_lock_ = 0;
    unsigned tx_selftest_reclaimed_ = 0;

    static void tx_selftest_reclaim(void* context, const tx_request*, int status) {
        auto* controller = static_cast<rtl8139_controller*>(context);
        if (controller != nullptr && status == 0)
            ++controller->tx_selftest_reclaimed_;
    }

    void lock_process() {
        uint32_t expected = 0;
        while (!__atomic_compare_exchange_n(&process_lock_, &expected, 1, false,
                                             __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            expected = 0;
            asm volatile("pause");
        }
    }

    void unlock_process() {
        __atomic_store_n(&process_lock_, 0, __ATOMIC_RELEASE);
    }

    uint8_t* rx_ring() const {
        return rx_ring_dma_.allocated
                   ? static_cast<uint8_t*>(rx_ring_dma_.value.virtual_address)
                   : nullptr;
    }

    bool physical_address_ok(uint64_t physical) const {
        return physical != UINT64_MAX && physical <= UINT32_MAX;
    }

    bool reset_hardware() {
        registers_.write8(register_offset::command, astia::rtl8139::command_reset);
        for (uint32_t spin = 0; spin < 1000000; ++spin) {
            if ((registers_.read8(register_offset::command) & astia::rtl8139::command_reset) == 0)
                return true;
            asm volatile("pause");
        }
        return false;
    }

    bool configure_hardware() {
        if (!physical_address_ok(rx_ring_dma_.value.physical))
            return false;
        registers_.write32(register_offset::rx_buffer_start,
                           static_cast<uint32_t>(rx_ring_dma_.value.physical));
        registers_.write32(register_offset::rx_config, (3u << 8) | 0x0fu);
        registers_.write32(register_offset::tx_config, 6u << 8);
        registers_.write8(register_offset::command,
                          astia::rtl8139::command_receiver_enable |
                              astia::rtl8139::command_transmitter_enable);
        registers_.write16(register_offset::interrupt_status, 0xffffu);
        registers_.write16(register_offset::interrupt_mask, astia::rtl8139::interrupt_rx_tx);
        rx_cursor_ = 0;
        registers_.write16(register_offset::current_address,
                           astia::rtl8139::capr_for_cursor(rx_cursor_));
        return true;
    }

    bool request_irq() {
        if (pci_ == nullptr || pci_->msix == 0)
            return false;
        if (astia_module_pci_msix_request(pci_->opaque, irq_entry, this, &irq_route_) != 0)
            return false;
        if (astia_module_pci_msix_program(&irq_route_) != 0 ||
            astia_module_pci_msix_enable(&irq_route_) != 0 ||
            astia_module_pci_msix_unmask(&irq_route_) != 0) {
            astia_module_pci_msix_release(&irq_route_);
            irq_route_ = {};
            return false;
        }
        irq_active_ = true;
        return true;
    }

    void release_irq() {
        if (!irq_active_)
            return;
        astia_module_pci_msix_disable(&irq_route_);
        astia_module_pci_msix_release(&irq_route_);
        irq_route_ = {};
        irq_active_ = false;
    }

    void recycle_rx(uint16_t next) {
        rx_cursor_ = next;
        registers_.write16(register_offset::current_address,
                           astia::rtl8139::capr_for_cursor(rx_cursor_));
    }

    bool inspect_and_dispatch(rx_packet& packet) {
        const rx_inspect_result result =
            astia::rtl8139::inspect_rx_packet(rx_ring(), rx_cursor_, packet);
        if (result == rx_inspect_result::empty)
            return false;
        if (result == rx_inspect_result::malformed) {
            const uint16_t wire_length = static_cast<uint16_t>(
                astia::rtl8139::ring_byte(rx_ring(), rx_cursor_ + 2) |
                static_cast<uint16_t>(astia::rtl8139::ring_byte(rx_ring(), rx_cursor_ + 3) << 8));
            if (wire_length >= astia::rtl8139::rx_crc_size &&
                wire_length <= astia::rtl8139::rx_max_frame_with_crc) {
                recycle_rx(astia::rtl8139::ring_offset(static_cast<uint32_t>(rx_cursor_) +
                                                       astia::rtl8139::rx_header_size +
                                                       wire_length + 3u));
                return true;
            }
            return false;
        }

        astia::net::packet_view view{};
        view.first = {packet.view.first.data, packet.view.first.length};
        view.second = {packet.view.second.data, packet.view.second.length};
        view.length = packet.view.length;
        view.wire_length = packet.view.wire_length;
        view.generation = net_device_.generation();
        rx_action action = net_device_.invoke_rx_irq(view);
        if (action == rx_action::defer) {
            if (rx_deferred_.push(packet))
                return false;
            action = rx_action::drop;
        }
        if (action == rx_action::recycle || action == rx_action::drop)
            recycle_rx(packet.view.next_offset);
        return true;
    }

    void drain_rx_irq() {
        const uint16_t hardware_cursor = static_cast<uint16_t>(
            registers_.read16(register_offset::current_buffer) & (astia::rtl8139::rx_ring_size - 1u));
        rx_packet packet{};
        for (uint32_t count = 0; count < max_irq_rx_packets && rx_cursor_ != hardware_cursor;
             ++count) {
            if (!inspect_and_dispatch(packet))
                break;
        }
    }

    void drain_tx_irq() {
        for (uint32_t index = 0; index < astia::rtl8139::tx_descriptor_count; ++index) {
            tx_slot& slot = tx_slots_[index];
            if (!slot.active)
                continue;
            const uint32_t status = registers_.read32(
                offset_for(static_cast<uint16_t>(register_offset::tx_status0) + index * 4u));
            if ((status & astia::rtl8139::tx_status_own) == 0)
                continue;
            if (!slot.completion_queued) {
                slot.completion_status = (status & astia::rtl8139::tx_status_ok) != 0 ? 0 : -5;
                slot.completion_queued = true;
            }
            if (net_device_.tx().complete(slot.request, slot.completion_status))
                slot = {};
        }
    }

    int available_tx_slot() const {
        for (uint32_t i = 0; i < astia::rtl8139::tx_descriptor_count; ++i)
            if (!tx_slots_[i].active)
                return static_cast<int>(i);
        return -1;
    }

    void cancel_all_process() {
        for (tx_slot& slot : tx_slots_) {
            if (!slot.active)
                continue;
            net_device_.tx().abort_inflight(slot.request, -125);
            slot = {};
        }
        net_device_.tx().reset();
        rx_packet packet{};
        while (rx_deferred_.pop(packet))
            recycle_rx(packet.view.next_offset);
    }

    int attach(astia_module_pci_device* device) {
        if (!pci_match(device) || state_ != device_state::detached)
            return -19;
        uint8_t bar = 0xff;
        for (uint8_t index = 0; index < device->bar_count && index < 6; ++index) {
            if (device->bar_present[index] != 0 && device->bar_io[index] == 0) {
                bar = index;
                break;
            }
        }
        if (bar == 0xff || astia_module_pci_enable_bus_master(device->opaque) != 0 ||
            astia_module_pci_map_bar(device->opaque, bar) != 0)
            return -19;
        const uint64_t mapped = astia_module_pci_bar_mapped(device->opaque, bar);
        const uint64_t size = astia_module_pci_bar_size(device->opaque, bar);
        if (mapped == 0 || size < 0x100)
            return -19;
        pci_ = device;
        registers_ = register_file(reinterpret_cast<volatile uint8_t*>(mapped), size);
        if (!rx_ring_dma_.allocate(astia::rtl8139::rx_ring_pages, true) ||
            !reset_hardware() || !configure_hardware()) {
            rx_ring_dma_.release();
            pci_ = nullptr;
            registers_ = {};
            return -12;
        }
        uint8_t mac[6]{};
        for (unsigned i = 0; i < 6; ++i)
            mac[i] = registers_.read8(offset_for(i));
        net_device_.set_mac(mac);
        net_device_.set_mtu(1500);
        net_device_.set_link_flags(astia::net::link_up | astia::net::link_running);
        net_device_.set_generation(generation_);
        net_device_.set_running(true);
        state_ = request_irq() ? device_state::running : device_state::probed_no_irq;
        astia_module_log("rtl8139", "probe ok");
        return 0;
    }

    public:
    static void irq_entry(void* context) {
        static_cast<rtl8139_controller*>(context)->interrupt();
    }

    int probe(astia_module_pci_device* device) { return attach(device); }

    void set_callbacks(rx_irq_callback immediate, rx_deferred_callback deferred, void* context) {
        net_device_.set_callbacks(immediate, deferred, context);
    }

    int submit(const tx_request& request) {
        if ((state_ != device_state::running && state_ != device_state::probed_no_irq) ||
            request.data == nullptr || request.physical > UINT32_MAX || request.length == 0 ||
            request.length > astia::rtl8139::tx_max_frame || request.reclaim == nullptr)
            return -22;
        return net_device_.tx().submit(request);
    }

    unsigned service_tx() {
        lock_process();
        unsigned submitted = 0;
        tx_request request{};
        while (available_tx_slot() >= 0 && net_device_.tx().dequeue(request)) {
            const unsigned slot_index = static_cast<unsigned>(available_tx_slot());
            tx_slot& slot = tx_slots_[slot_index];
            slot.active = true;
            slot.request = request;
            registers_.write32(offset_for(static_cast<uint16_t>(register_offset::tx_address0) +
                                          slot_index * 4u), request.physical);
            registers_.write32(offset_for(static_cast<uint16_t>(register_offset::tx_status0) +
                                          slot_index * 4u), request.length);
            ++submitted;
        }
        unlock_process();
        return submitted;
    }

    unsigned poll_completions() {
        lock_process();
        unsigned count = net_device_.tx().poll();
        if (net_device_.running()) {
            rx_packet packet{};
            if (rx_deferred_.pop(packet)) {
                astia::net::packet_view view{};
                view.first = {packet.view.first.data, packet.view.first.length};
                view.second = {packet.view.second.data, packet.view.second.length};
                view.length = packet.view.length;
                view.wire_length = packet.view.wire_length;
                view.generation = net_device_.generation();
                net_device_.invoke_rx_deferred(view);
                recycle_rx(packet.view.next_offset);
            }
        }
        unlock_process();
        return count;
    }

    void interrupt() {
        if (state_ != device_state::running)
            return;
        const uint16_t status = registers_.read16(register_offset::interrupt_status);
        if (status == 0 || status == 0xffff)
            return;
        registers_.write16(register_offset::interrupt_status, status);
        if ((status & (astia::rtl8139::interrupt_rx_ok | astia::rtl8139::interrupt_rx_error |
                       astia::rtl8139::interrupt_rx_overflow |
                       astia::rtl8139::interrupt_fifo_overflow)) != 0)
            drain_rx_irq();
        if ((status & (astia::rtl8139::interrupt_tx_ok | astia::rtl8139::interrupt_tx_error)) != 0)
            drain_tx_irq();
    }

    int reset_process() {
        if (state_ == device_state::detached)
            return -19;
        lock_process();
        release_irq();
        registers_.write16(register_offset::interrupt_mask, 0);
        registers_.write8(register_offset::command, 0);
        cancel_all_process();
        ++generation_;
        if (generation_ == 0)
            generation_ = 1;
        net_device_.set_generation(generation_);
        if (!reset_hardware() || !configure_hardware()) {
            state_ = device_state::reset;
            net_device_.set_running(false);
            unlock_process();
            return -5;
        }
        state_ = request_irq() ? device_state::running : device_state::probed_no_irq;
        net_device_.set_running(true);
        unlock_process();
        return 0;
    }

    int detach_process() {
        if (state_ == device_state::detached)
            return 0;
        lock_process();
        release_irq();
        registers_.write16(register_offset::interrupt_mask, 0);
        registers_.write8(register_offset::command, 0);
        cancel_all_process();
        rx_ring_dma_.release();
        pci_ = nullptr;
        registers_ = {};
        net_device_.set_running(false);
        state_ = device_state::detached;
        unlock_process();
        return 0;
    }

    int selftest() {
        uint8_t byte = 0;
        tx_request request{};
        request.data = &byte;
        request.length = 1;
        request.reclaim = tx_selftest_reclaim;
        const int submitted = net_device_.tx().submit(request);
        if (submitted != 0) {
            astia_module_log_u64("rtl8139", "public tx submit failed", submitted);
            return 0;
        }
        tx_request active{};
        if (!net_device_.tx().dequeue(active)) {
            astia_module_log("rtl8139", "public tx dequeue failed");
            return 0;
        }
        if (!net_device_.tx().complete(active, 0)) {
            astia_module_log("rtl8139", "public tx complete failed");
            return 0;
        }
        if (net_device_.tx().poll() != 1 || tx_selftest_reclaimed_ != 1) {
            astia_module_log("rtl8139", "public tx reclaim failed");
            return 0;
        }
        astia_module_log("rtl8139", "public tx selftest ok");
        return 1;
    }

    void initialize() {
        net_device_.initialize("rtl8139", this);
    }

    net_device* device() { return &net_device_; }
};

rtl8139_controller controller;

} // namespace

extern "C" int rtl8139_pci_probe(astia_module_pci_device* device) {
    return controller.probe(device);
}

extern "C" int rtl8139_set_callbacks(astia::rtl8139::rx_irq_callback immediate,
                                       astia::rtl8139::rx_deferred_callback deferred,
                                       void* context) {
    controller.set_callbacks(immediate, deferred, context);
    return 0;
}

extern "C" int rtl8139_submit(const astia::rtl8139::tx_request* request) {
    return request == nullptr ? -22 : controller.submit(*request);
}

extern "C" unsigned rtl8139_service_tx() {
    return controller.service_tx();
}

extern "C" unsigned rtl8139_poll() {
    return controller.poll_completions();
}

extern "C" int rtl8139_reset() {
    return controller.reset_process();
}

extern "C" int rtl8139_detach() {
    return controller.detach_process();
}

extern "C" int rtl8139_pci_match(const astia_module_pci_device* device) {
    return astia::rtl8139::pci_match(device) ? 1 : 0;
}

extern "C" int rtl8139_pci_probe_net(const astia_module_pci_device* device,
                                       astia::net::netdev** value) {
    if (value == nullptr || device == nullptr)
        return -22;
    const int result = controller.probe(const_cast<astia_module_pci_device*>(device));
    if (result == 0)
        *value = controller.device();
    return result;
}

extern "C" void rtl8139_pci_remove_net(astia::net::netdev*) {
    controller.detach_process();
}

extern "C" int rtl8139_selftest() {
    return controller.selftest();
}

extern "C" int module_main() {
    controller.initialize();
    static const astia::module::net_driver driver{
        "rtl8139", rtl8139_pci_match, rtl8139_pci_probe_net, rtl8139_pci_remove_net,
        rtl8139_selftest};
    return astia::module::register_net_driver(driver);
}
