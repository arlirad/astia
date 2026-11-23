#pragma once

#include <astia/core/endian.hpp>
#include <astia/hw/pci.hpp>

#include "usb_protocol.hpp"

#include <stddef.h>
#include <stdint.h>

namespace astia::usb::xhci {

    enum class enumeration_state : uint8_t {
        capabilities_only,
        unsupported_no_transfer_engine
    };

    struct capability_snapshot {
        uint8_t  capability_length      = 0;
        uint16_t interface_version      = 0;
        uint8_t  max_slots              = 0;
        uint16_t max_interrupters       = 0;
        uint8_t  max_ports              = 0;
        uint16_t max_scratchpad_buffers = 0;
        bool     context_size_64        = false;
        bool     address_64             = false;
        uint32_t doorbell_offset        = 0;
        uint32_t runtime_offset         = 0;
        uint32_t operational_status     = 0;
        uint32_t page_size_mask         = 0;
        uint32_t configure              = 0;
        uint32_t first_port_status      = 0;
        bool     ready                  = false;
        bool     halted                 = false;
        bool     page_size_4k           = false;
    };

    struct probe_result {
        bool                valid = false;
        enumeration_state   state = enumeration_state::capabilities_only;
        capability_snapshot capabilities{};
    };

    int enumerate_pci_port(astia::hw::pci_device* device, uint8_t port,
                           device_model& result);

    enum class trb_type : uint8_t {
        normal                   = 1,
        setup_stage              = 2,
        data_stage               = 3,
        status_stage             = 4,
        link                     = 6,
        enable_slot              = 9,
        address_device           = 11,
        configure_endpoint       = 12,
        evaluate_context         = 13,
        stop_endpoint            = 15,
        reset_endpoint           = 16,
        command_completion_event = 33,
        transfer_event           = 32,
        port_status_change_event = 34,
    };

    struct trb {
        astia::core::le<uint64_t> parameter{};
        astia::core::le<uint32_t> status{};
        astia::core::le<uint32_t> control{};
    };

    static_assert(sizeof(trb) == 16, "xHCI TRB layout");

    constexpr uint32_t trb_cycle                   = 1u;
    constexpr uint32_t trb_toggle_cycle            = 1u << 1;
    constexpr uint32_t trb_chain                   = 1u << 4;
    constexpr uint32_t trb_interrupt_on_completion = 1u << 5;
    constexpr uint32_t trb_immediate_data          = 1u << 6;
    constexpr uint32_t trb_direction_in            = 1u << 16;

    constexpr uint32_t trb_type_bits(trb_type type) {
        return static_cast<uint32_t>(type) << 10;
    }

    constexpr unsigned trb_transfer_type_shift   = 16;
    constexpr uint32_t trb_transfer_type_no_data = 0u << trb_transfer_type_shift;
    constexpr uint32_t trb_transfer_type_out     = 2u << trb_transfer_type_shift;
    constexpr uint32_t trb_transfer_type_in      = 3u << trb_transfer_type_shift;

    constexpr unsigned command_slot_shift = 24;

    constexpr uint32_t command_slot_bits(uint8_t slot) {
        return static_cast<uint32_t>(slot) << command_slot_shift;
    }

    constexpr uint8_t slot_context_speed(usb_speed speed) {
        switch (speed) {
        case usb_speed::full:
            return 1;
        case usb_speed::low:
            return 2;
        case usb_speed::high:
            return 3;
        case usb_speed::super:
            return 4;
        case usb_speed::super_plus:
            return 5;
        }
        return 0;
    }

    inline trb make_setup_trb(const setup_packet& setup, bool cycle) {
        const uint64_t value = static_cast<uint64_t>(setup.request_type) |
                               (static_cast<uint64_t>(setup.request) << 8) |
                               (static_cast<uint64_t>(setup.value) << 16) |
                               (static_cast<uint64_t>(setup.index) << 32) |
                               (static_cast<uint64_t>(setup.length) << 48);
        const uint32_t transfer_type =
            setup.length == 0 ? trb_transfer_type_no_data
                              : (setup.request_type & 0x80u ? trb_transfer_type_in
                                                            : trb_transfer_type_out);
        trb result{};
        result.parameter = value;
        result.status    = 8u;
        result.control   = trb_type_bits(trb_type::setup_stage) | trb_immediate_data |
                         transfer_type | (cycle ? trb_cycle : 0u);
        return result;
    }

    inline trb make_data_trb(uint64_t physical, uint32_t length, bool in,
                             uint32_t td_size, bool cycle, bool chain = true) {
        trb result{};
        result.parameter = physical;
        result.status    = (length & 0x1ffffu) | ((td_size & 0x1fu) << 17);
        result.control   = trb_type_bits(trb_type::data_stage) |
                         (in ? trb_direction_in : 0u) | (chain ? trb_chain : 0u) |
                         (cycle ? trb_cycle : 0u);
        return result;
    }

    inline trb make_status_trb(bool in, bool cycle) {
        trb result{};
        result.control = trb_type_bits(trb_type::status_stage) |
                         trb_interrupt_on_completion | (in ? trb_direction_in : 0u) |
                         (cycle ? trb_cycle : 0u);
        return result;
    }

    inline trb make_link_trb(uint64_t physical, bool cycle) {
        trb result{};
        result.parameter = physical;
        result.control =
            trb_type_bits(trb_type::link) | trb_toggle_cycle | (cycle ? trb_cycle : 0u);
        return result;
    }

    template <unsigned Capacity>
    class control_ring {
        static_assert(Capacity >= 4,
                      "control ring must hold one control transfer and a link");
        trb*     entries_  = nullptr;
        uint64_t physical_ = 0;
        unsigned enqueue_  = 0;
        bool     cycle_    = true;
        bool     busy_     = false;

        void advance() {
            ++enqueue_;
            if (enqueue_ != Capacity - 1)
                return;
            entries_[enqueue_] = make_link_trb(physical_, cycle_);
            cycle_             = !cycle_;
            enqueue_           = 0;
        }

        public:
        control_ring() = default;

        void initialize(void* virtual_address, uint64_t physical_address) {
            entries_  = static_cast<trb*>(virtual_address);
            physical_ = physical_address;
            enqueue_  = 0;
            cycle_    = true;
            busy_     = false;
            if (entries_ != nullptr) {
                for (unsigned i = 0; i < Capacity; ++i)
                    entries_[i] = {};
                entries_[Capacity - 1] = make_link_trb(physical_, true);
            }
        }

        bool ready() const {
            return entries_ != nullptr;
        }
        bool busy() const {
            return busy_;
        }
        unsigned enqueue_index() const {
            return enqueue_;
        }
        bool cycle_state() const {
            return cycle_;
        }

        bool enqueue(const setup_packet& setup, uint64_t data_physical,
                     uint32_t data_length, uint16_t max_packet_size) {
            if (!ready() || busy_ || setup.length != data_length ||
                data_length > 0x1ffffu || (data_length != 0 && data_physical == 0) ||
                max_packet_size == 0)
                return false;
            const uint32_t packets =
                (data_length + max_packet_size - 1) / max_packet_size;
            const uint32_t data_trbs = (packets + 31) / 32;
            if (data_trbs + 2 > Capacity - 1)
                return false;
            const unsigned setup_index = enqueue_;
            entries_[setup_index]      = make_setup_trb(setup, cycle_);
            advance();
            if (data_length != 0) {
                uint32_t remaining = data_length;
                uint64_t physical  = data_physical;
                while (remaining != 0) {
                    const uint32_t packets =
                        (remaining + max_packet_size - 1) / max_packet_size;
                    const uint32_t packet_count = packets > 32 ? 32 : packets;
                    const uint32_t chunk = remaining < packet_count * max_packet_size
                                               ? remaining
                                               : packet_count * max_packet_size;
                    entries_[enqueue_] =
                        make_data_trb(physical, chunk, (setup.request_type & 0x80u) != 0,
                                      packets > 32 ? 31 : packets - 1, cycle_, true);
                    advance();
                    physical += chunk;
                    remaining -= chunk;
                }
            }
            entries_[enqueue_] = make_status_trb(
                data_length == 0 || (setup.request_type & 0x80u) == 0, cycle_);
            advance();
            busy_ = true;
            return true;
        }

        void retire() {
            busy_ = false;
        }
        const trb& at(unsigned index) const {
            return entries_[index];
        }
    };

    enum class completion_code : uint8_t {
        invalid                = 0,
        success                = 1,
        data_buffer_error      = 2,
        babble_detected        = 3,
        usb_transaction_error  = 4,
        trb_error              = 5,
        stall_error            = 6,
        short_packet           = 13,
        stopped                = 26,
        stopped_length_invalid = 27,
        stopped_short_packet   = 28,
    };

    struct control_request {
        transfer_state state = transfer_state::free;
        setup_packet   setup{};
        void*          buffer     = nullptr;
        uint64_t       physical   = 0;
        uint32_t       length     = 0;
        int64_t        result     = 0;
        uint16_t       generation = 0;
    };

    template <unsigned Capacity, unsigned RingCapacity = 16>
    class control_transfer_engine {
        control_request            requests_[Capacity]{};
        control_ring<RingCapacity> ring_{};

        bool owns(transfer_handle handle) const {
            return handle.valid() && handle.slot < Capacity &&
                   requests_[handle.slot].generation == handle.generation &&
                   requests_[handle.slot].state != transfer_state::free;
        }

        static int64_t completion_result(completion_code code, uint32_t length,
                                         uint32_t residual) {
            if (residual > length)
                return -5;
            switch (code) {
            case completion_code::success:
            case completion_code::short_packet:
            case completion_code::stopped_short_packet:
                return static_cast<int64_t>(length - residual);
            case completion_code::stall_error:
                return -32;
            case completion_code::stopped:
            case completion_code::stopped_length_invalid:
                return -125;
            default:
                return -5;
            }
        }

        public:
        bool initialize_ring(void* virtual_address, uint64_t physical_address) {
            ring_.initialize(virtual_address, physical_address);
            return ring_.ready();
        }

        const control_ring<RingCapacity>& ring() const {
            return ring_;
        }

        bool submit(const setup_packet& setup, void* buffer, uint64_t physical,
                    uint32_t length, uint16_t max_packet_size, transfer_handle& handle) {
            handle = {};
            if (setup.length != length ||
                (length != 0 && (buffer == nullptr || physical == 0)))
                return false;
            for (unsigned i = 0; i < Capacity; ++i) {
                control_request& request = requests_[i];
                if (request.state != transfer_state::free)
                    continue;
                ++request.generation;
                if (request.generation == 0)
                    ++request.generation;
                if (!ring_.enqueue(setup, physical, length, max_packet_size))
                    return false;
                request = {transfer_state::pending, setup, buffer, physical, length, 0,
                           request.generation};
                handle  = {static_cast<uint16_t>(i), request.generation};
                return true;
            }
            return false;
        }

        bool cancel(transfer_handle handle) {
            if (!owns(handle) || requests_[handle.slot].state != transfer_state::pending)
                return false;
            requests_[handle.slot].state = transfer_state::cancel_requested;
            return true;
        }

        bool complete(transfer_handle handle, completion_code code, uint32_t residual) {
            if (!owns(handle))
                return false;
            control_request& request = requests_[handle.slot];
            if (request.state != transfer_state::pending &&
                request.state != transfer_state::cancel_requested)
                return false;
            request.result = request.state == transfer_state::cancel_requested
                                 ? -125
                                 : completion_result(code, request.length, residual);
            request.state  = request.state == transfer_state::cancel_requested
                                 ? transfer_state::canceled
                             : request.result < 0 ? transfer_state::failed
                                                  : transfer_state::completed;
            ring_.retire();
            return true;
        }

        bool release(transfer_handle handle) {
            if (!owns(handle))
                return false;
            control_request& request = requests_[handle.slot];
            if (request.state != transfer_state::completed &&
                request.state != transfer_state::failed &&
                request.state != transfer_state::canceled)
                return false;
            request = {transfer_state::free, {}, nullptr, 0, 0, 0, request.generation};
            return true;
        }

        transfer_state state(transfer_handle handle) const {
            return owns(handle) ? requests_[handle.slot].state : transfer_state::free;
        }

        int64_t result(transfer_handle handle) const {
            return owns(handle) ? requests_[handle.slot].result : -22;
        }

        const control_request* request(transfer_handle handle) const {
            return owns(handle) ? &requests_[handle.slot] : nullptr;
        }
    };

    inline trb make_normal_trb(uint64_t physical, uint32_t length, uint8_t interrupter,
                               bool cycle, bool interrupt_on_short = true) {
        trb result{};
        result.parameter = physical;
        result.status    = (length & 0x1ffffu) | (1u << 17);
        result.control   = trb_type_bits(trb_type::normal) | trb_interrupt_on_completion |
                         (interrupt_on_short ? (1u << 2) : 0u) |
                         (static_cast<uint32_t>(interrupter) << 22) |
                         (cycle ? trb_cycle : 0u);
        return result;
    }

    struct interrupt_request {
        transfer_state state      = transfer_state::free;
        void*          buffer     = nullptr;
        uint64_t       physical   = 0;
        uint32_t       length     = 0;
        uint32_t       actual     = 0;
        int64_t        result     = 0;
        uint16_t       generation = 0;
        trb            descriptor{};
    };

    template <unsigned Capacity>
    class interrupt_transfer_engine {
        interrupt_request requests_[Capacity]{};
        bool              cycle_ = true;

        bool owns(transfer_handle handle) const {
            return handle.valid() && handle.slot < Capacity &&
                   requests_[handle.slot].generation == handle.generation &&
                   requests_[handle.slot].state != transfer_state::free;
        }

        static int64_t completion_result(completion_code code, uint32_t length,
                                         uint32_t residual) {
            if (residual > length)
                return -5;
            switch (code) {
            case completion_code::success:
            case completion_code::short_packet:
            case completion_code::stopped_short_packet:
                return static_cast<int64_t>(length - residual);
            case completion_code::stall_error:
                return -32;
            case completion_code::stopped:
            case completion_code::stopped_length_invalid:
                return -125;
            default:
                return -5;
            }
        }

        public:
        bool submit(void* buffer, uint64_t physical, uint32_t length, uint8_t interrupter,
                    transfer_handle& handle) {
            handle = {};
            if (buffer == nullptr || physical == 0 || length == 0)
                return false;
            for (unsigned i = 0; i < Capacity; ++i) {
                interrupt_request& request = requests_[i];
                if (request.state != transfer_state::free)
                    continue;
                uint16_t generation = static_cast<uint16_t>(request.generation + 1);
                if (generation == 0)
                    generation = 1;
                request            = {};
                request.state      = transfer_state::pending;
                request.buffer     = buffer;
                request.physical   = physical;
                request.length     = length;
                request.generation = generation;
                request.descriptor =
                    make_normal_trb(physical, length, interrupter, cycle_);
                handle = {static_cast<uint16_t>(i), generation};
                if (i + 1 == Capacity)
                    cycle_ = !cycle_;
                return true;
            }
            return false;
        }

        bool cancel(transfer_handle handle) {
            if (!owns(handle) || requests_[handle.slot].state != transfer_state::pending)
                return false;
            requests_[handle.slot].state = transfer_state::cancel_requested;
            return true;
        }

        bool complete(transfer_handle handle, completion_code code, uint32_t residual) {
            if (!owns(handle))
                return false;
            interrupt_request& request = requests_[handle.slot];
            if (request.state != transfer_state::pending &&
                request.state != transfer_state::cancel_requested)
                return false;
            if (request.state == transfer_state::cancel_requested) {
                request.state  = transfer_state::canceled;
                request.result = -125;
                request.actual = 0;
            }
            else {
                request.result = completion_result(code, request.length, residual);
                request.actual =
                    residual > request.length ? 0 : request.length - residual;
                request.state = request.result < 0 ? transfer_state::failed
                                                   : transfer_state::completed;
            }
            return true;
        }

        bool release(transfer_handle handle) {
            if (!owns(handle))
                return false;
            interrupt_request& request = requests_[handle.slot];
            if (request.state != transfer_state::completed &&
                request.state != transfer_state::failed &&
                request.state != transfer_state::canceled)
                return false;
            const uint16_t generation = request.generation;
            request                   = {};
            request.generation        = generation;
            return true;
        }

        transfer_state state(transfer_handle handle) const {
            return owns(handle) ? requests_[handle.slot].state : transfer_state::free;
        }

        int64_t result(transfer_handle handle) const {
            return owns(handle) ? requests_[handle.slot].result : -22;
        }

        uint32_t actual(transfer_handle handle) const {
            return owns(handle) ? requests_[handle.slot].actual : 0;
        }

        const trb* descriptor(transfer_handle handle) const {
            return owns(handle) ? &requests_[handle.slot].descriptor : nullptr;
        }
    };

    class deferred_interrupts {
        volatile bool pending_ = false;
        bool          enabled_ = true;

        public:
        void irq() {
            if (enabled_)
                pending_ = true;
        }

        bool take() {
            if (!pending_)
                return false;
            pending_ = false;
            return true;
        }

        void mask() {
            enabled_ = false;
        }
        void unmask() {
            enabled_ = true;
        }
        bool pending() const {
            return pending_;
        }
    };

    enum class enumeration_phase : uint8_t {
        idle,
        device_descriptor_short,
        device_descriptor,
        set_address,
        configuration_header,
        configuration,
        set_configuration,
        complete,
        unsupported,
        error,
    };

    class enumeration_session {
        enumeration_phase phase_ = enumeration_phase::idle;
        device_model      device_{};
        uint8_t           port_                 = 0;
        uint8_t           address_              = 0;
        uint16_t          configuration_length_ = 0;

        public:
        void clear() {
            auto* bytes = reinterpret_cast<uint8_t*>(&device_);
            for (size_t index = 0; index < sizeof(device_); ++index)
                bytes[index] = 0;
            phase_                = enumeration_phase::idle;
            port_                 = 0;
            address_              = 0;
            configuration_length_ = 0;
        }

        bool begin(unsigned slot, uint8_t port, uint8_t address, usb_speed speed) {
            if (slot >= max_devices || port == 0 || address == 0)
                return false;
            clear();
            device_.present = true;
            device_.address = address;
            device_.speed   = speed;
            phase_          = enumeration_phase::device_descriptor_short;
            port_           = port;
            address_        = address;
            return true;
        }

        enumeration_phase phase() const {
            return phase_;
        }
        const device_model& device() const {
            return device_;
        }
        uint8_t port() const {
            return port_;
        }
        uint8_t address() const {
            return address_;
        }

        bool next(setup_packet& setup, uint16_t& length) const {
            switch (phase_) {
            case enumeration_phase::device_descriptor_short:
                setup  = get_descriptor_request(descriptor_device, 0, 8);
                length = 8;
                return true;
            case enumeration_phase::device_descriptor:
                setup  = get_descriptor_request(descriptor_device, 0, 18);
                length = 18;
                return true;
            case enumeration_phase::set_address:
                return false;
            case enumeration_phase::configuration_header:
                setup  = get_descriptor_request(descriptor_configuration, 0, 9);
                length = 9;
                return true;
            case enumeration_phase::configuration:
                setup  = get_descriptor_request(descriptor_configuration, 0,
                                                configuration_length_);
                length = configuration_length_;
                return true;
            case enumeration_phase::set_configuration:
                setup  = set_configuration_request(device_.configurations[0].value);
                length = 0;
                return true;
            default:
                return false;
            }
        }

        bool complete(const uint8_t* data, uint16_t length) {
            if (length != 0 && data == nullptr)
                return false;
            switch (phase_) {
            case enumeration_phase::device_descriptor_short:
                if (length < 8 || data[0] < 8 || data[1] != descriptor_device ||
                    !valid_packet_size0(data[7], device_.speed)) {
                    phase_ = enumeration_phase::error;
                    return false;
                }
                phase_ = enumeration_phase::device_descriptor;
                return true;
            case enumeration_phase::device_descriptor:
                if (!parse_device_descriptor(data, length, device_.descriptor,
                                             device_.speed)) {
                    phase_ = enumeration_phase::error;
                    return false;
                }
                phase_ = enumeration_phase::set_address;
                return true;
            case enumeration_phase::set_address:
                phase_ = enumeration_phase::configuration_header;
                return true;
            case enumeration_phase::configuration_header: {
                if (length < 9 || data[1] != descriptor_configuration) {
                    phase_ = enumeration_phase::error;
                    return false;
                }
                configuration_length_ = le16(data + 2);
                if (configuration_length_ < 9 || configuration_length_ > 4096) {
                    phase_ = enumeration_phase::unsupported;
                    return false;
                }
                phase_ = enumeration_phase::configuration;
                return true;
            }
            case enumeration_phase::configuration:
                if (length != configuration_length_ ||
                    !parse_configuration_descriptor(
                        data, length, device_.configurations[0],
                        device_.speed == usb_speed::super ||
                            device_.speed == usb_speed::super_plus)) {
                    phase_ = enumeration_phase::error;
                    return false;
                }
                device_.configuration_count = 1;
                phase_                      = enumeration_phase::set_configuration;
                return true;
            case enumeration_phase::set_configuration:
                phase_ = enumeration_phase::complete;
                return true;
            default:
                return false;
            }
        }

        bool complete_address() {
            if (phase_ != enumeration_phase::set_address)
                return false;
            phase_ = enumeration_phase::configuration_header;
            return true;
        }
    };

    inline bool read8(const volatile uint8_t* base, uint64_t size, uint64_t offset,
                      uint8_t& result) {
        if (base == nullptr || offset >= size)
            return false;
        result = base[offset];
        return true;
    }

    inline bool read16(const volatile uint8_t* base, uint64_t size, uint64_t offset,
                       uint16_t& result) {
        if (base == nullptr || (offset & 1u) != 0 || offset > size || size - offset < 2)
            return false;
        result = *reinterpret_cast<volatile const uint16_t*>(base + offset);
        return true;
    }

    inline bool read32(const volatile uint8_t* base, uint64_t size, uint64_t offset,
                       uint32_t& result) {
        if (base == nullptr || (offset & 3u) != 0 || offset > size || size - offset < 4)
            return false;
        result = *reinterpret_cast<volatile const uint32_t*>(base + offset);
        return true;
    }

    inline bool probe(const volatile uint8_t* base, uint64_t bar_size,
                      probe_result& result) {
        result              = {};
        uint8_t  cap_length = 0;
        uint16_t version    = 0;
        uint32_t params1    = 0;
        uint32_t params2    = 0;
        uint32_t hccparams  = 0;
        uint32_t doorbell   = 0;
        uint32_t runtime    = 0;
        if (!read8(base, bar_size, 0, cap_length) ||
            !read16(base, bar_size, 2, version) || !read32(base, bar_size, 4, params1) ||
            !read32(base, bar_size, 8, params2) ||
            !read32(base, bar_size, 0x10, hccparams) ||
            !read32(base, bar_size, 0x14, doorbell) ||
            !read32(base, bar_size, 0x18, runtime))
            return false;
        const uint64_t operational     = cap_length;
        const uint64_t doorbell_offset = doorbell & ~3u;
        const uint64_t runtime_offset  = runtime & ~0x1fu;
        const uint64_t extended_offset = static_cast<uint64_t>(hccparams >> 16) << 2;
        if (cap_length < 0x20 || operational > bar_size ||
            bar_size - operational < 0x404 || (doorbell & 3u) != 0 ||
            doorbell_offset > bar_size || bar_size - doorbell_offset < 4 ||
            (runtime & 0x1fu) != 0 || runtime_offset > bar_size ||
            bar_size - runtime_offset < 0x20 ||
            (extended_offset != 0 &&
             (extended_offset > bar_size || bar_size - extended_offset < 4)))
            return false;
        result.capabilities.capability_length = cap_length;
        result.capabilities.interface_version = version;
        result.capabilities.max_slots         = static_cast<uint8_t>(params1 & 0xffu);
        result.capabilities.max_interrupters =
            static_cast<uint16_t>((params1 >> 8) & 0x7ffu);
        result.capabilities.max_ports              = static_cast<uint8_t>(params1 >> 24);
        result.capabilities.max_scratchpad_buffers = static_cast<uint16_t>(
            ((params2 >> 21) & 0x1fu) << 5 | ((params2 >> 27) & 0x1fu));
        result.capabilities.context_size_64 = (hccparams & (1u << 2)) != 0;
        result.capabilities.address_64      = (hccparams & 1u) != 0;
        result.capabilities.doorbell_offset = static_cast<uint32_t>(doorbell_offset);
        result.capabilities.runtime_offset  = static_cast<uint32_t>(runtime_offset);
        if (result.capabilities.max_slots == 0 ||
            result.capabilities.max_interrupters == 0 ||
            result.capabilities.max_ports == 0)
            return false;
        uint32_t status      = 0;
        uint32_t page_sizes  = 0;
        uint32_t configure   = 0;
        uint32_t port_status = 0;
        if (!read32(base, bar_size, operational + 4, status) ||
            !read32(base, bar_size, operational + 8, page_sizes) ||
            !read32(base, bar_size, operational + 0x38, configure) ||
            !read32(base, bar_size, operational + 0x400, port_status))
            return false;
        result.capabilities.operational_status = status;
        result.capabilities.page_size_mask     = page_sizes & 0xffffu;
        result.capabilities.configure          = configure;
        result.capabilities.first_port_status  = port_status;
        result.capabilities.ready              = (status & (1u << 11)) == 0;
        result.capabilities.halted             = (status & 1u) != 0;
        result.capabilities.page_size_4k       = (page_sizes & 1u) != 0;
        result.valid = result.capabilities.ready && result.capabilities.page_size_4k;
        result.state = enumeration_state::unsupported_no_transfer_engine;
        return result.valid;
    }

}
