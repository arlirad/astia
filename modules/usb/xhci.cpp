#include <astia/module_abi.hpp>
#include <astia/dev/regs.hpp>

#include "xhci.hpp"
#include "hid_keyboard.hpp"

namespace astia::usb::xhci {

bool pci_match(const astia_module_pci_device* device);

namespace {

constexpr unsigned timeout_spins = 4000000;
constexpr unsigned command_ring_capacity = 16;
constexpr unsigned transfer_ring_capacity = 64;
constexpr unsigned event_capacity = 32;
constexpr unsigned endpoint_control = 1;
constexpr unsigned operational_run_stop = 1u;
constexpr unsigned operational_host_reset = 1u << 1;
constexpr unsigned status_halted = 1u;
constexpr unsigned status_controller_not_ready = 1u << 11;
constexpr unsigned port_connected = 1u;
constexpr unsigned port_enabled = 1u << 1;
constexpr unsigned port_reset = 1u << 4;
constexpr unsigned port_speed_shift = 10;
constexpr unsigned context_entries_shift = 27;
constexpr unsigned slot_speed_shift = 20;
constexpr unsigned slot_port_shift = 16;
constexpr unsigned endpoint_type_control = 4u << 3;
constexpr unsigned endpoint_error_count = 3u << 1;
constexpr unsigned address_bsr = 1u << 9;

using dma = astia::module::dma_buffer;

class mmio {
    public:
    mmio() = default;
    explicit mmio(volatile void* base) : bytes_(base) {}

    template <typename T>
    T read(unsigned offset) const {
        uint8_t raw[sizeof(T)]{};
        for (unsigned i = 0; i < sizeof(T); ++i)
            raw[i] = bytes_[offset + i];
        return astia::core::le<T>::load(raw);
    }

    template <typename T>
    void write(unsigned offset, T value) const {
        astia::core::le<T> encoded(value);
        uint8_t raw[sizeof(T)]{};
        encoded.store(raw);
        for (unsigned i = 0; i < sizeof(T); ++i)
            bytes_[offset + i] = raw[i];
    }

    bool mapped() const { return bytes_.base() != nullptr; }

    private:
    astia::dev::regs<uint8_t> bytes_{};
};

struct erst_entry {
    astia::core::le<uint64_t> address{};
    astia::core::le<uint16_t> size{};
    astia::core::le<uint16_t> reserved{};
    astia::core::le<uint32_t> reserved2{};
};

static_assert(sizeof(erst_entry) == 16, "xHCI event ring segment layout");

void memory_barrier() {
    asm volatile("mfence" ::: "memory");
}

void clear_bytes(void* address, unsigned length) {
    auto* bytes = static_cast<uint8_t*>(address);
    for (unsigned i = 0; i < length; ++i)
        bytes[i] = 0;
}

void store32(void* address, unsigned offset, uint32_t value) {
    astia::core::le<uint32_t> encoded(value);
    encoded.store(static_cast<uint8_t*>(address) + offset);
}

void store64(void* address, unsigned offset, uint64_t value) {
    astia::core::le<uint64_t> encoded(value);
    encoded.store(static_cast<uint8_t*>(address) + offset);
}

uint8_t completion_slot(const trb& event) {
    return static_cast<uint8_t>(event.control.get() >> 24);
}

completion_code completion(const trb& event) {
    return static_cast<completion_code>(event.status.get() >> 24);
}

uint32_t residual(const trb& event) {
    return event.status.get() & 0x00ffffffu;
}

class controller {
    void* pci_ = nullptr;
    mmio common_{};
    mmio doorbells_{};
    mmio runtime_{};
    capability_snapshot capabilities_{};
    dma dcbaa_{};
    dma command_memory_{};
    dma event_memory_{};
    dma erst_memory_{};
    dma input_memory_{};
    dma device_context_{};
    dma transfer_memory_{};
    control_transfer_engine<1, transfer_ring_capacity> control_{};
    unsigned command_enqueue_ = 0;
    bool command_cycle_ = true;
    unsigned event_dequeue_ = 0;
    bool event_cycle_ = true;
    uint64_t command_physical_ = 0;
    uint64_t event_physical_ = 0;
    uint8_t slot_ = 0;
    uint8_t next_address_ = 1;
    usb_speed speed_ = usb_speed::full;
    bool started_ = false;

    uint32_t operational(unsigned offset) const {
        return static_cast<uint32_t>(capabilities_.capability_length) + offset;
    }

    bool wait_status(uint32_t mask, uint32_t expected) const {
        for (unsigned spin = 0; spin < timeout_spins; ++spin) {
            if ((common_.read<uint32_t>(operational(4)) & mask) == expected)
                return true;
            asm volatile("pause");
        }
        return false;
    }

    bool reset() {
        uint32_t command = common_.read<uint32_t>(operational(0));
        command &= ~operational_run_stop;
        common_.write<uint32_t>(operational(0), command);
        if (!wait_status(status_halted, status_halted))
            return false;
        common_.write<uint32_t>(operational(0), operational_host_reset);
        if (!wait_status(operational_host_reset, 0) ||
            !wait_status(status_controller_not_ready, 0))
            return false;
        return true;
    }

    bool allocate_memory() {
        if (!dcbaa_.allocate(1) || !command_memory_.allocate(1) || !event_memory_.allocate(1) ||
            !erst_memory_.allocate(1) || !input_memory_.allocate(1) ||
            !device_context_.allocate(1) || !transfer_memory_.allocate(2))
            return false;
        clear_bytes(dcbaa_.value.virtual_address, ASTIA_PAGE_SIZE);
        clear_bytes(command_memory_.value.virtual_address, ASTIA_PAGE_SIZE);
        clear_bytes(event_memory_.value.virtual_address, ASTIA_PAGE_SIZE);
        clear_bytes(erst_memory_.value.virtual_address, ASTIA_PAGE_SIZE);
        clear_bytes(input_memory_.value.virtual_address, ASTIA_PAGE_SIZE);
        clear_bytes(device_context_.value.virtual_address, ASTIA_PAGE_SIZE);
        clear_bytes(transfer_memory_.value.virtual_address, ASTIA_PAGE_SIZE * 2);
        command_physical_ = command_memory_.value.physical;
        event_physical_ = event_memory_.value.physical;
        auto* command = static_cast<trb*>(command_memory_.value.virtual_address);
        command[command_ring_capacity - 1] = make_link_trb(command_physical_, true);
        auto* erst = static_cast<erst_entry*>(erst_memory_.value.virtual_address);
        erst[0].address = event_physical_;
        erst[0].size = event_capacity;
        control_.initialize_ring(transfer_memory_.value.virtual_address,
                                 transfer_memory_.value.physical);
        return true;
    }

    void advance_command() {
        ++command_enqueue_;
        if (command_enqueue_ != command_ring_capacity - 1) {
            return;
        }
        auto* command = static_cast<trb*>(command_memory_.value.virtual_address);
        command[command_enqueue_] = make_link_trb(command_physical_, command_cycle_);
        command_cycle_ = !command_cycle_;
        command_enqueue_ = 0;
    }

    bool enqueue_command(uint64_t parameter, uint32_t control, uint64_t& address) {
        auto* command = static_cast<trb*>(command_memory_.value.virtual_address);
        address = command_physical_ + command_enqueue_ * sizeof(trb);
        command[command_enqueue_] = {};
        command[command_enqueue_].parameter = parameter;
        command[command_enqueue_].control = control | (command_cycle_ ? trb_cycle : 0u);
        advance_command();
        memory_barrier();
        doorbells_.write<uint32_t>(0, 0);
        return true;
    }

    bool next_event(trb& result) {
        auto* events = static_cast<volatile trb*>(event_memory_.value.virtual_address);
        memory_barrier();
        const volatile trb& event = events[event_dequeue_];
        if ((event.control.get() & trb_cycle) != (event_cycle_ ? trb_cycle : 0u))
            return false;
        result.parameter = event.parameter.get();
        result.status = event.status.get();
        result.control = event.control.get();
        ++event_dequeue_;
        if (event_dequeue_ == event_capacity) {
            event_dequeue_ = 0;
            event_cycle_ = !event_cycle_;
        }
        runtime_.write<uint64_t>(0x18, event_physical_ + event_dequeue_ * sizeof(trb));
        return true;
    }

    bool wait_command(uint64_t command_address, uint8_t* slot_result = nullptr) {
        for (unsigned spin = 0; spin < timeout_spins; ++spin) {
            trb event{};
            if (!next_event(event)) {
                asm volatile("pause");
                continue;
            }
            if (static_cast<trb_type>((event.control.get() >> 10) & 0x3fu) !=
                trb_type::command_completion_event)
                continue;
            if (event.parameter.get() != command_address)
                continue;
            if (completion(event) != completion_code::success)
                return false;
            if (slot_result != nullptr)
                *slot_result = completion_slot(event);
            return true;
        }
        return false;
    }

    bool configure_interrupter() {
        auto* erst = static_cast<erst_entry*>(erst_memory_.value.virtual_address);
        runtime_.write<uint32_t>(0x00, 0);
        runtime_.write<uint32_t>(0x08, 1);
        runtime_.write<uint64_t>(0x10, erst_memory_.value.physical);
        runtime_.write<uint64_t>(0x18, event_physical_);
        return runtime_.read<uint64_t>(0x10) == erst_memory_.value.physical &&
               erst[0].address.get() == event_physical_;
    }

    bool configure_controller() {
        common_.write<uint64_t>(operational(0x30), dcbaa_.value.physical);
        common_.write<uint64_t>(operational(0x18), command_physical_ | 1u);
        common_.write<uint32_t>(operational(0x38), 1);
        if (!configure_interrupter())
            return false;
        common_.write<uint32_t>(operational(0), operational_run_stop);
        return wait_status(status_halted, 0);
    }

    bool enable_slot() {
        uint64_t command_address = 0;
        if (!enqueue_command(0, trb_type_bits(trb_type::enable_slot), command_address))
            return false;
        uint8_t result = 0;
        if (!wait_command(command_address, &result) || result == 0 ||
            result > capabilities_.max_slots)
            return false;
        slot_ = result;
        return true;
    }

    void make_input_context(uint8_t port, uint8_t speed, uint16_t max_packet_size) {
        const unsigned context_size = capabilities_.context_size_64 ? 64 : 32;
        clear_bytes(input_memory_.value.virtual_address, ASTIA_PAGE_SIZE);
        store32(input_memory_.value.virtual_address, 4, (1u << 0) | (1u << endpoint_control));
        store32(input_memory_.value.virtual_address, context_size + 0,
                (static_cast<uint32_t>(speed) << slot_speed_shift) |
                    (1u << context_entries_shift));
        store32(input_memory_.value.virtual_address, context_size + 4,
                static_cast<uint32_t>(port) << slot_port_shift);
        store32(input_memory_.value.virtual_address, context_size + context_size + 4,
                endpoint_error_count | endpoint_type_control |
                    (static_cast<uint32_t>(max_packet_size) << 16));
        store64(input_memory_.value.virtual_address, context_size + context_size + 8,
                transfer_memory_.value.physical | 1u);
        store32(input_memory_.value.virtual_address, context_size + context_size + 16, 8);
        store64(device_context_.value.virtual_address, 0, 0);
        store64(static_cast<uint8_t*>(dcbaa_.value.virtual_address) + slot_ * 8, 0,
                device_context_.value.physical);
    }

    bool address_device(bool block_address, uint8_t port, uint8_t speed,
                        uint16_t max_packet_size) {
        make_input_context(port, speed, max_packet_size);
        uint64_t command_address = 0;
        const uint32_t control = trb_type_bits(trb_type::address_device) |
                                 (block_address ? address_bsr : 0u);
        if (!enqueue_command(input_memory_.value.physical, control, command_address))
            return false;
        return wait_command(command_address);
    }

    bool reset_port(uint8_t port) {
        if (port == 0 || port > capabilities_.max_ports)
            return false;
        const unsigned offset = operational(0x400 + (port - 1) * 0x10);
        uint32_t status = common_.read<uint32_t>(offset);
        if ((status & port_connected) == 0)
            return false;
        common_.write<uint32_t>(offset, status | port_reset);
        for (unsigned spin = 0; spin < timeout_spins; ++spin) {
            status = common_.read<uint32_t>(offset);
            if ((status & port_reset) == 0)
                break;
            asm volatile("pause");
        }
        if ((status & port_reset) != 0 || (status & port_enabled) == 0)
            return false;
        const uint8_t value = static_cast<uint8_t>((status >> port_speed_shift) & 0xfu);
        switch (value) {
        case 1:
            speed_ = usb_speed::full;
            break;
        case 2:
            speed_ = usb_speed::low;
            break;
        case 3:
            speed_ = usb_speed::high;
            break;
        case 4:
            speed_ = usb_speed::super;
            break;
        case 5:
            speed_ = usb_speed::super_plus;
            break;
        default:
            return false;
        }
        return true;
    }

    int transfer(const setup_packet& setup, uint16_t max_packet_size) {
        transfer_handle handle{};
        auto* buffer = static_cast<uint8_t*>(transfer_memory_.value.virtual_address) +
                       ASTIA_PAGE_SIZE;
        const uint32_t length = setup.length;
        if (length > ASTIA_PAGE_SIZE ||
            !control_.submit(setup, length == 0 ? nullptr : buffer,
                              length == 0 ? 0 : transfer_memory_.value.physical + ASTIA_PAGE_SIZE,
                              length, max_packet_size, handle))
            return -22;
        doorbells_.write<uint32_t>(slot_ * 4, endpoint_control);
        for (unsigned spin = 0; spin < timeout_spins; ++spin) {
            trb event{};
            if (!next_event(event)) {
                asm volatile("pause");
                continue;
            }
            if (static_cast<trb_type>((event.control.get() >> 10) & 0x3fu) !=
                    trb_type::transfer_event ||
                completion_slot(event) != slot_)
                continue;
            const int64_t result = control_.complete(handle, completion(event), residual(event))
                                       ? control_.result(handle)
                                       : -5;
            if (result < 0) {
                (void)control_.release(handle);
                return static_cast<int>(result);
            }
            (void)control_.release(handle);
            return static_cast<int>(result);
        }
        if (control_.cancel(handle)) {
            for (unsigned spin = 0; spin < timeout_spins / 16; ++spin) {
                trb event{};
                if (!next_event(event)) {
                    asm volatile("pause");
                    continue;
                }
                if (static_cast<trb_type>((event.control.get() >> 10) & 0x3fu) ==
                        trb_type::transfer_event &&
                    completion_slot(event) == slot_) {
                    (void)control_.complete(handle, completion(event), residual(event));
                    (void)control_.release(handle);
                    break;
                }
            }
        }
        return -110;
    }

    bool enumerate(uint8_t port, device_model& result) {
        if (!started_ || !reset_port(port) || !enable_slot())
            return false;
        const uint8_t address = next_address_ == 0 || next_address_ > 127 ? 1 : next_address_++;
        const uint8_t speed = static_cast<uint8_t>(speed_);
        if (!address_device(true, port, speed, 8))
            return false;
        enumeration_session session;
        if (!session.begin(slot_, port, address, speed_))
            return false;
        uint8_t descriptor[4096]{};
        for (;;) {
            if (session.phase() == enumeration_phase::set_address) {
                if (!address_device(false, port, speed,
                                    session.device().descriptor.max_packet_size0) ||
                    !session.complete_address())
                    return false;
                continue;
            }
            setup_packet setup{};
            uint16_t length = 0;
            if (!session.next(setup, length))
                break;
            const int transfer_result = transfer(setup, session.device().descriptor.max_packet_size0 == 0
                                                             ? 8
                                                             : session.device().descriptor.max_packet_size0);
            if (transfer_result < 0 || static_cast<unsigned>(transfer_result) > length)
                return false;
            for (unsigned i = 0; i < static_cast<unsigned>(transfer_result); ++i)
                descriptor[i] = static_cast<uint8_t*>(transfer_memory_.value.virtual_address)[ASTIA_PAGE_SIZE + i];
            if (!session.complete(length == 0 ? nullptr : descriptor,
                                  static_cast<uint16_t>(transfer_result)))
                return false;
            if (session.phase() == enumeration_phase::complete)
                break;
        }
        if (session.phase() != enumeration_phase::complete)
            return false;
        result = session.device();
        return true;
    }

    public:
    int start(const astia_module_pci_device* device) {
        if (started_ || device == nullptr || !pci_match(device))
            return -19;
        pci_ = device->opaque;
        if (astia_module_pci_enable_bus_master(pci_) != 0 ||
            astia_module_pci_disable_msix(pci_) != 0 ||
            astia_module_pci_map_bar(pci_, 0) != 0)
            return -19;
        const uint64_t mapped = astia_module_pci_bar_mapped(pci_, 0);
        const uint64_t size = astia_module_pci_bar_size(pci_, 0);
        if (mapped == 0 || size < 0x800)
            return -95;
        probe_result checked{};
        if (!probe(reinterpret_cast<volatile const uint8_t*>(mapped), size, checked) ||
            checked.capabilities.max_scratchpad_buffers != 0 || !checked.valid)
            return -95;
        capabilities_ = checked.capabilities;
        common_ = mmio{reinterpret_cast<volatile void*>(mapped)};
        doorbells_ = mmio{reinterpret_cast<volatile void*>(mapped + capabilities_.doorbell_offset)};
        runtime_ = mmio{reinterpret_cast<volatile void*>(mapped + capabilities_.runtime_offset + 0x20)};
        if (!reset() || !allocate_memory() || !configure_controller()) {
            stop();
            return -5;
        }
        started_ = true;
        return 0;
    }

    void stop() {
        if (common_.mapped()) {
            const uint32_t command = common_.read<uint32_t>(operational(0));
            common_.write<uint32_t>(operational(0), command & ~operational_run_stop);
        }
        started_ = false;
        transfer_memory_.release();
        device_context_.release();
        input_memory_.release();
        erst_memory_.release();
        event_memory_.release();
        command_memory_.release();
        dcbaa_.release();
        common_ = {};
        doorbells_ = {};
        runtime_ = {};
        pci_ = nullptr;
    }

    int enumerate_port(uint8_t port, device_model& result) { return enumerate(port, result) ? 0 : -95; }

    uint8_t max_ports() const { return capabilities_.max_ports; }
};

controller controller_instance;

}

bool pci_match(const astia_module_pci_device* device) {
    return device != nullptr && device->class_code == 0x0c && device->subclass == 0x03 &&
           device->programming_interface == 0x30 && device->bar_count != 0 &&
           device->bar_present[0] != 0 && device->bar_io[0] == 0;
}

int pci_capability_probe(const astia_module_pci_device* device, probe_result& result) {
    if (!pci_match(device))
        return -19;
    if (astia_module_pci_map_bar(device->opaque, 0) != 0)
        return -19;
    const uint64_t mapped = astia_module_pci_bar_mapped(device->opaque, 0);
    const uint64_t size = astia_module_pci_bar_size(device->opaque, 0);
    if (mapped == 0 || size < 0x20)
        return -19;
    if (!probe(reinterpret_cast<volatile const uint8_t*>(mapped), size, result))
        return -95;
    return -95;
}

int enumerate_pci_port(const astia_module_pci_device* device, uint8_t port,
                       device_model& result) {
    const int start_result = controller_instance.start(device);
    if (start_result != 0)
        return start_result;
    return controller_instance.enumerate_port(port, result);
}

}

namespace astia::usb::xhci {

namespace {

int probe_pci(const astia_module_pci_device* device, void** context) {
    if (context == nullptr || !pci_match(device))
        return -19;
    *context = nullptr;
    const int result = controller_instance.start(device);
    if (result != 0) {
        return result;
    }
    device_model model{};
    for (uint8_t port = 1; port <= controller_instance.max_ports(); ++port) {
        if (controller_instance.enumerate_port(port, model) != 0)
            continue;
        *context = &controller_instance;
        return 0;
    }
    controller_instance.stop();
    return -19;
}

int match_pci(const astia_module_pci_device* device) {
    return pci_match(device) ? 1 : 0;
}

void remove_pci(void*) {
    controller_instance.stop();
}

const astia_module_pci_driver pci_driver{"xhci", match_pci, probe_pci, remove_pci};

}

}

extern "C" int module_main() {
    return astia_module_register_pci_driver(&astia::usb::xhci::pci_driver);
}
