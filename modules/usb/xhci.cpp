#include "xhci.hpp"

#include <astia/mm/dma_buffer.hpp>
#include <astia/module.hpp>

#include "hid_keyboard.hpp"
#include "mmio_width.hpp"

#include <new>

namespace astia::usb::xhci {

    bool pci_match(const astia::hw::pci_device* device);

    namespace {
#ifdef ASTIA_USB_TEST_BAR_MAP_FAILURE
        bool map_bar(astia::hw::pci_device&) {
            return false;
        }
#else
        bool map_bar(astia::hw::pci_device& device) {
            return astia::module::pci_map_bar(device, 0);
        }
#endif

        constexpr unsigned timeout_spins               = 4000000;
        constexpr unsigned command_ring_capacity       = 16;
        constexpr unsigned transfer_ring_capacity      = 64;
        constexpr unsigned event_capacity              = 32;
        constexpr unsigned endpoint_control            = 1;
        constexpr unsigned operational_run_stop        = 1u;
        constexpr unsigned operational_host_reset      = 1u << 1;
        constexpr unsigned status_halted               = 1u;
        constexpr unsigned status_controller_not_ready = 1u << 11;
        constexpr unsigned port_connected              = 1u;
        constexpr unsigned port_enabled                = 1u << 1;
        constexpr unsigned port_reset                  = 1u << 4;
        constexpr unsigned port_speed_shift            = 10;
        constexpr unsigned context_entries_shift       = 27;
        constexpr unsigned slot_speed_shift            = 20;
        constexpr unsigned slot_port_shift             = 16;
        constexpr unsigned endpoint_type_control       = 4u << 3;
        constexpr unsigned endpoint_error_count        = 3u << 1;
        constexpr unsigned address_bsr                 = 1u << 9;

        using dma = astia::mm::dma_buffer;

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

        class exclusive_scope {
            bool& active_;

            public:
            explicit exclusive_scope(bool& active) : active_(active) {
                active_ = true;
            }
            ~exclusive_scope() {
                active_ = false;
            }
            exclusive_scope(const exclusive_scope&)            = delete;
            exclusive_scope& operator=(const exclusive_scope&) = delete;
        };

        class controller {
            astia::hw::pci_device*                             pci_ = nullptr;
            mmio                                               common_{};
            mmio                                               doorbells_{};
            mmio                                               runtime_{};
            capability_snapshot                                capabilities_{};
            dma                                                dcbaa_{};
            dma                                                command_memory_{};
            dma                                                event_memory_{};
            dma                                                erst_memory_{};
            dma                                                input_memory_{};
            dma                                                device_context_{};
            dma                                                transfer_memory_{};
            control_transfer_engine<1, transfer_ring_capacity> control_{};
            enumeration_session                                enumeration_{};
            uint8_t   enumeration_descriptor_[4096]{};
            bool      enumerating_        = false;
            unsigned  command_enqueue_    = 0;
            bool      command_cycle_      = true;
            unsigned  event_dequeue_      = 0;
            bool      event_cycle_        = true;
            uint64_t  command_physical_   = 0;
            uint64_t  event_physical_     = 0;
            uint8_t   slot_               = 0;
            uint8_t   next_address_       = 1;
            usb_speed speed_              = usb_speed::full;
            bool      started_            = false;
            bool      quarantined_        = false;
            bool      bar_mapped_         = false;
            bool      bus_master_changed_ = false;
            bool      msix_modified_      = false;
            uint16_t  original_command_   = 0;
            uint16_t  original_msix_      = 0;

            void abandon_dma() {
                transfer_memory_.abandon();
                device_context_.abandon();
                input_memory_.abandon();
                erst_memory_.abandon();
                event_memory_.abandon();
                command_memory_.abandon();
                dcbaa_.abandon();
            }

            void rollback_pci() {
                if (bar_mapped_) {
                    (void) astia::module::pci_unmap_bar(pci_, 0);
                    bar_mapped_ = false;
                }
                if (msix_modified_) {
                    (void) astia::module::pci_restore_msix(pci_, original_msix_);
                    msix_modified_ = false;
                }
                if (bus_master_changed_) {
                    (void) astia::module::pci_disable_bus_master(pci_);
                    bus_master_changed_ = false;
                }
            }

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
                if (!dcbaa_.allocate(1) || !command_memory_.allocate(1) ||
                    !event_memory_.allocate(1) || !erst_memory_.allocate(1) ||
                    !input_memory_.allocate(1) || !device_context_.allocate(1) ||
                    !transfer_memory_.allocate(2))
                    return false;
                clear_bytes(dcbaa_.data(), astia::mm::page_size);
                clear_bytes(command_memory_.data(), astia::mm::page_size);
                clear_bytes(event_memory_.data(), astia::mm::page_size);
                clear_bytes(erst_memory_.data(), astia::mm::page_size);
                clear_bytes(input_memory_.data(), astia::mm::page_size);
                clear_bytes(device_context_.data(), astia::mm::page_size);
                clear_bytes(transfer_memory_.data(), astia::mm::page_size * 2);
                command_physical_ = command_memory_.physical();
                event_physical_   = event_memory_.physical();
                auto* command     = static_cast<trb*>(command_memory_.data());
                command[command_ring_capacity - 1] =
                    make_link_trb(command_physical_, true);
                auto* erst      = static_cast<erst_entry*>(erst_memory_.data());
                erst[0].address = event_physical_;
                erst[0].size    = event_capacity;
                control_.initialize_ring(transfer_memory_.data(),
                                         transfer_memory_.physical());
                return true;
            }

            void advance_command() {
                ++command_enqueue_;
                if (command_enqueue_ != command_ring_capacity - 1) {
                    return;
                }
                auto* command = static_cast<trb*>(command_memory_.data());
                command[command_enqueue_] =
                    make_link_trb(command_physical_, command_cycle_);
                command_cycle_   = !command_cycle_;
                command_enqueue_ = 0;
            }

            bool enqueue_command(uint8_t slot, uint64_t parameter, uint32_t control,
                                 uint64_t& address) {
                auto* command = static_cast<trb*>(command_memory_.data());
                address       = command_physical_ + command_enqueue_ * sizeof(trb);
                command[command_enqueue_]           = {};
                command[command_enqueue_].parameter = parameter;
                command[command_enqueue_].control =
                    control | command_slot_bits(slot) | (command_cycle_ ? trb_cycle : 0u);
                advance_command();
                memory_barrier();
                doorbells_.write<uint32_t>(0, 0);
                return true;
            }

            bool next_event(trb& result) {
                auto* events = static_cast<volatile trb*>(event_memory_.data());
                memory_barrier();
                const volatile trb& event = events[event_dequeue_];
                if ((event.control.get() & trb_cycle) != (event_cycle_ ? trb_cycle : 0u))
                    return false;
                result.parameter = event.parameter.get();
                result.status    = event.status.get();
                result.control   = event.control.get();
                ++event_dequeue_;
                if (event_dequeue_ == event_capacity) {
                    event_dequeue_ = 0;
                    event_cycle_   = !event_cycle_;
                }
                runtime_.write<uint64_t>(0x18,
                                         event_physical_ + event_dequeue_ * sizeof(trb));
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
                auto* erst = static_cast<erst_entry*>(erst_memory_.data());
                runtime_.write<uint32_t>(0x00, 0);
                runtime_.write<uint32_t>(0x08, 1);
                runtime_.write<uint64_t>(0x10, erst_memory_.physical());
                runtime_.write<uint64_t>(0x18, event_physical_);
                return runtime_.read<uint64_t>(0x10) == erst_memory_.physical() &&
                       erst[0].address.get() == event_physical_;
            }

            bool configure_controller() {
                const uint64_t crcr = command_physical_ | 1u;
                common_.write<uint64_t>(operational(0x30), dcbaa_.physical());
                common_.write<uint64_t>(operational(0x18), crcr);
                astia::module::log_u64("usb", "xhci crcr", crcr);
                common_.write<uint32_t>(operational(0x38), 1);
                if (!configure_interrupter())
                    return false;
                common_.write<uint32_t>(operational(0), operational_run_stop);
                return wait_status(status_halted, 0);
            }

            bool enable_slot() {
                uint64_t command_address = 0;
                if (!enqueue_command(0, 0, trb_type_bits(trb_type::enable_slot),
                                     command_address))
                    return false;
                uint8_t result = 0;
                if (!wait_command(command_address, &result) || result == 0 ||
                    result > capabilities_.max_slots)
                    return false;
                slot_ = result;
                astia::module::log_u64("usb", "xhci enable slot trb", command_address);
                astia::module::log_u64("usb", "xhci enable slot", result);
                return true;
            }

            void make_input_context(uint8_t port, uint8_t slot_speed,
                                    uint16_t max_packet_size) {
                const unsigned context_size = capabilities_.context_size_64 ? 64 : 32;
                clear_bytes(input_memory_.data(), astia::mm::page_size);
                store32(input_memory_.data(), 4, (1u << 0) | (1u << endpoint_control));
                store32(input_memory_.data(), context_size + 0,
                        (static_cast<uint32_t>(slot_speed) << slot_speed_shift) |
                            (1u << context_entries_shift));
                store32(input_memory_.data(), context_size + 4,
                        static_cast<uint32_t>(port) << slot_port_shift);
                store32(input_memory_.data(), context_size + context_size + 4,
                        endpoint_error_count | endpoint_type_control |
                            (static_cast<uint32_t>(max_packet_size) << 16));
                store64(input_memory_.data(), context_size + context_size + 8,
                        transfer_memory_.physical() | 1u);
                store32(input_memory_.data(), context_size + context_size + 16, 8);
                store64(device_context_.data(), 0, 0);
                store64(static_cast<uint8_t*>(dcbaa_.data()) + slot_ * 8, 0,
                        device_context_.physical());
            }

            uint16_t initial_control_packet_size() const {
                switch (speed_) {
                case usb_speed::high:
                    return 64;
                case usb_speed::super:
                case usb_speed::super_plus:
                    return 512;
                case usb_speed::low:
                case usb_speed::full:
                default:
                    return 8;
                }
            }

            bool address_device(bool block_address, uint8_t port, usb_speed speed,
                                uint16_t max_packet_size) {
                const uint8_t slot_speed = slot_context_speed(speed);
                if (slot_speed == 0)
                    return false;
                make_input_context(port, slot_speed, max_packet_size);
                uint64_t       command_address = 0;
                const uint32_t control         = trb_type_bits(trb_type::address_device) |
                                         (block_address ? address_bsr : 0u);
                if (!enqueue_command(slot_, input_memory_.physical(), control,
                                     command_address) ||
                    !wait_command(command_address))
                    return false;
                astia::module::log_u64("usb", "xhci address device", slot_);
                return true;
            }

            bool reset_port(uint8_t port) {
                if (port == 0 || port > capabilities_.max_ports)
                    return false;
                const unsigned offset = operational(0x400 + (port - 1) * 0x10);
                uint32_t       status = common_.read<uint32_t>(offset);
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
                const uint8_t value =
                    static_cast<uint8_t>((status >> port_speed_shift) & 0xfu);
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
                auto*           buffer =
                    static_cast<uint8_t*>(transfer_memory_.data()) + astia::mm::page_size;
                const uint32_t length = setup.length;
                if (length > astia::mm::page_size ||
                    !control_.submit(setup, length == 0 ? nullptr : buffer,
                                     length == 0 ? 0
                                                 : transfer_memory_.physical() +
                                                       astia::mm::page_size,
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
                    const int64_t result =
                        control_.complete(handle, completion(event), residual(event))
                            ? control_.result(handle)
                            : -5;
                    if (result < 0) {
                        (void) control_.release(handle);
                        return static_cast<int>(result);
                    }
                    (void) control_.release(handle);
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
                            (void) control_.complete(handle, completion(event),
                                                     residual(event));
                            (void) control_.release(handle);
                            break;
                        }
                    }
                }
                return -110;
            }

            bool enumerate(uint8_t port, device_model& result) {
                if (!started_ || enumerating_)
                    return false;
                exclusive_scope scope(enumerating_);
                clear_bytes(enumeration_descriptor_, sizeof enumeration_descriptor_);
                enumeration_.clear();
                if (!reset_port(port) || !enable_slot())
                    return false;
                const uint8_t address =
                    next_address_ == 0 || next_address_ > 127 ? 1 : next_address_++;
                if (!address_device(true, port, speed_, initial_control_packet_size()))
                    return false;
                enumeration_session& session = enumeration_;
                if (!session.begin(slot_, port, address, speed_))
                    return false;
                uint8_t* descriptor = enumeration_descriptor_;
                for (;;) {
                    if (session.phase() == enumeration_phase::set_address) {
                        if (!address_device(
                                false, port, speed_,
                                session.device().descriptor.max_packet_size0) ||
                            !session.complete_address())
                            return false;
                        continue;
                    }
                    setup_packet setup{};
                    uint16_t     length = 0;
                    if (!session.next(setup, length))
                        break;
                    const int transfer_result = transfer(
                        setup, session.device().descriptor.max_packet_size0 == 0
                                   ? 8
                                   : session.device().descriptor.max_packet_size0);
                    if (transfer_result < 0 ||
                        static_cast<unsigned>(transfer_result) > length)
                        return false;
                    for (unsigned i = 0; i < static_cast<unsigned>(transfer_result); ++i)
                        descriptor[i] = static_cast<uint8_t*>(
                            transfer_memory_.data())[astia::mm::page_size + i];
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
            int start(astia::hw::pci_device* device) {
                if (started_ || quarantined_ || device == nullptr || !pci_match(device))
                    return -19;
                pci_                = device;
                original_command_   = astia::module::pci_read16(pci_, 0x04);
                bus_master_changed_ = (original_command_ & (1u << 2)) == 0;
                if (!astia::module::pci_enable_bus_master(pci_))
                    return rollback_pci(), -19;
                if (pci_->interrupt.msix) {
                    original_msix_ = astia::module::pci_read16(
                        pci_, static_cast<uint16_t>(pci_->interrupt.capability + 2));
                }
                if (!astia::module::pci_disable_msix(pci_))
                    return rollback_pci(), -19;
                msix_modified_ = pci_->interrupt.msix;
                if (!map_bar(*pci_)) {
                    rollback_pci();
#ifdef ASTIA_USB_TEST_BAR_MAP_FAILURE
                    const uint16_t control = astia::module::pci_read16(
                        pci_, static_cast<uint16_t>(pci_->interrupt.capability + 2));
                    const uint16_t command = astia::module::pci_read16(pci_, 0x04);
                    const bool     restored =
                        (control & 0xc000u) == (original_msix_ & 0xc000u) &&
                        (command & 4u) == (original_command_ & 4u) &&
                        !pci_->bars[0].mapped_valid;
                    astia::module::log("usb-test",
                                       restored ? "rollback-ok" : "rollback-failed");
#endif
                    return -19;
                }
                bar_mapped_           = true;
                const uint64_t mapped = astia::module::pci_bar_mapped(pci_, 0);
                const uint64_t size   = astia::module::pci_bar_size(pci_, 0);
                if (mapped == 0 || size < 0x800)
                    return rollback_pci(), -95;
                astia::module::log("usb", "xhci bar mapped");
                probe_result checked{};
                if (!probe(reinterpret_cast<volatile const uint8_t*>(mapped), size,
                           checked) ||
                    checked.capabilities.max_scratchpad_buffers != 0 || !checked.valid) {
                    astia::module::log("usb", "xhci probe failed");
                    return rollback_pci(), -95;
                }
                capabilities_ = checked.capabilities;
                astia::module::log("usb", "xhci probe ok");
                common_    = mmio{reinterpret_cast<volatile void*>(mapped)};
                doorbells_ = mmio{reinterpret_cast<volatile void*>(
                    mapped + capabilities_.doorbell_offset)};
                runtime_   = mmio{reinterpret_cast<volatile void*>(
                    mapped + capabilities_.runtime_offset + 0x20)};
                if (!reset()) {
                    astia::module::log("usb", "xhci reset failed");
                    (void) stop();
                    return -5;
                }
                astia::module::log("usb", "xhci reset ok");
                if (!allocate_memory()) {
                    astia::module::log("usb", "xhci dma failed");
                    (void) stop();
                    return -5;
                }
                astia::module::log_u64("usb", "xhci cmd ring", command_physical_);
                astia::module::log_u64("usb", "xhci event ring", event_physical_);
                astia::module::log_u64("usb", "xhci dcbaa", dcbaa_.physical());
                if (!configure_controller()) {
                    astia::module::log("usb", "xhci configure failed");
                    (void) stop();
                    return -5;
                }
                started_ = true;
                astia::module::log("usb", "xhci started");
                return 0;
            }

            bool stop() {
                if (quarantined_)
                    return false;
                if (common_.mapped()) {
                    const uint32_t command = common_.read<uint32_t>(operational(0));
                    common_.write<uint32_t>(
                        operational(0),
                        command & ~(operational_run_stop | (1u << 2) | (1u << 3)));
                    if (!wait_status(status_halted, status_halted)) {
                        quarantined_ = true;
                        abandon_dma();
                        return false;
                    }
                }
                started_ = false;
                transfer_memory_.release();
                device_context_.release();
                input_memory_.release();
                erst_memory_.release();
                event_memory_.release();
                command_memory_.release();
                dcbaa_.release();
                common_    = {};
                doorbells_ = {};
                runtime_   = {};
                rollback_pci();
                pci_ = nullptr;
                return true;
            }

            int enumerate_port(uint8_t port, device_model& result) {
                return enumerate(port, result) ? 0 : -95;
            }

            uint8_t max_ports() const {
                return capabilities_.max_ports;
            }
        };

        controller controller_instance;

    }

    bool pci_match(const astia::hw::pci_device* device) {
        return device != nullptr && device->class_code == 0x0c &&
               device->subclass == 0x03 && device->programming_interface == 0x30 &&
               device->bar_count != 0 && device->bars[0].present && !device->bars[0].io;
    }

    int pci_capability_probe(astia::hw::pci_device* device, probe_result& result) {
        if (!pci_match(device))
            return -19;
        if (!astia::module::pci_map_bar(device, 0))
            return -19;
        const auto     unmap  = [&] { (void) astia::module::pci_unmap_bar(device, 0); };
        const uint64_t mapped = astia::module::pci_bar_mapped(device, 0);
        const uint64_t size   = astia::module::pci_bar_size(device, 0);
        if (mapped == 0 || size < 0x20) {
            unmap();
            return -19;
        }
        const bool valid =
            probe(reinterpret_cast<volatile const uint8_t*>(mapped), size, result);
        unmap();
        if (!valid)
            return -95;
        return -95;
    }

    int enumerate_pci_port(astia::hw::pci_device* device, uint8_t port,
                           device_model& result) {
        const int start_result = controller_instance.start(device);
        if (start_result != 0)
            return start_result;
        return controller_instance.enumerate_port(port, result);
    }

}

namespace astia::usb::xhci {

    namespace {

        class xhci_driver final : public astia::hw::driver {
            public:
            const char* name() const override {
                return "xhci";
            }

            bool match(const astia::hw::device_info& info) const override {
                const auto* pci = astia::hw::pci_device_info::get(info);
                return pci != nullptr && pci_match(&pci->device());
            }

            bool probe(const astia::hw::device_info& info) override {
                const auto* pci = astia::hw::pci_device_info::get(info);
                if (pci == nullptr)
                    return false;
                auto* stable = astia::hw::pci_devices().find(pci->device().bdf);
                if (stable == nullptr || controller_instance.start(stable) != 0) {
                    astia::module::log("usb", "xhci start failed");
                    return false;
                }
                auto* model = new (std::nothrow) device_model{};
                if (model == nullptr) {
                    controller_instance.stop();
                    return false;
                }
                for (uint8_t port = 1; port <= controller_instance.max_ports(); ++port)
                    if (controller_instance.enumerate_port(port, *model) == 0) {
                        astia::module::log_u64("usb", "xhci port enumerated", port);
                        delete model;
                        return true;
                    }
                astia::module::log("usb", "xhci enumeration failed");
                delete model;
                controller_instance.stop();
                return false;
            }

            void remove(const astia::hw::device_info&) override {
                controller_instance.stop();
            }
        };

        xhci_driver driver;

    }

}

int module_main() {
    return astia::hw::drivers().register_driver(astia::usb::xhci::driver) ? 0 : -19;
}
