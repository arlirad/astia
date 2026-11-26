#pragma once

#include <astia/common.hpp>
#include <astia/hw/pci.hpp>
#include <astia/net.hpp>

#include <stdint.h>

namespace astia::rtl8139 {
    constexpr uint16_t pci_vendor_id             = 0x10ec;
    constexpr uint16_t pci_device_id             = 0x8139;
    constexpr uint8_t  pci_class_network         = 0x02;
    constexpr uint8_t  pci_subclass_ethernet     = 0x00;
    constexpr uint8_t  pci_programming_interface = 0x00;

    constexpr uint32_t rx_ring_size       = 8u * 1024u;
    constexpr uint32_t rx_ring_allocation = rx_ring_size + 16u;
    constexpr uint32_t rx_ring_pages =
        (rx_ring_allocation + astia::mm::page_size - 1u) / astia::mm::page_size;
    constexpr uint32_t rx_header_size        = 4;
    constexpr uint32_t rx_crc_size           = 4;
    constexpr uint32_t rx_max_frame_with_crc = 4096;
    constexpr uint32_t tx_descriptor_count   = 4;
    constexpr uint32_t tx_max_frame          = 1792;
    constexpr uint32_t tx_queue_depth        = 16;
    constexpr uint32_t tx_completion_depth   = tx_descriptor_count;
    constexpr uint32_t rx_deferred_depth     = 2;

    enum class register_offset : uint16_t {
        id0              = 0x00,
        multicast0       = 0x08,
        tx_status0       = 0x10,
        tx_address0      = 0x20,
        rx_buffer_start  = 0x30,
        command          = 0x37,
        current_address  = 0x38,
        current_buffer   = 0x3a,
        interrupt_mask   = 0x3c,
        interrupt_status = 0x3e,
        tx_config        = 0x40,
        rx_config        = 0x44,
        c_plus_command   = 0xe0,
    };

    constexpr uint8_t command_reset              = 1u << 4;
    constexpr uint8_t command_receiver_enable    = 1u << 3;
    constexpr uint8_t command_transmitter_enable = 1u << 2;
    constexpr uint8_t command_buffer_empty       = 1u << 0;

    constexpr uint16_t interrupt_rx_ok         = 1u << 0;
    constexpr uint16_t interrupt_rx_error      = 1u << 1;
    constexpr uint16_t interrupt_tx_ok         = 1u << 2;
    constexpr uint16_t interrupt_tx_error      = 1u << 3;
    constexpr uint16_t interrupt_rx_overflow   = 1u << 4;
    constexpr uint16_t interrupt_link_change   = 1u << 5;
    constexpr uint16_t interrupt_fifo_overflow = 1u << 6;
    constexpr uint16_t interrupt_rx_tx =
        interrupt_rx_ok | interrupt_rx_error | interrupt_tx_ok | interrupt_tx_error |
        interrupt_rx_overflow | interrupt_link_change | interrupt_fifo_overflow;

    constexpr uint16_t rx_status_ok              = 1u << 0;
    constexpr uint16_t rx_status_frame_alignment = 1u << 1;
    constexpr uint16_t rx_status_crc             = 1u << 2;
    constexpr uint16_t rx_status_long            = 1u << 3;
    constexpr uint16_t rx_status_runt            = 1u << 4;

    constexpr uint32_t tx_status_ok       = 1u << 15;
    constexpr uint32_t tx_status_underrun = 1u << 14;
    constexpr uint32_t tx_status_own      = 1u << 13;
    constexpr uint32_t tx_status_abort    = 1u << 30;

    constexpr uint16_t le16(const uint8_t* data) {
        return static_cast<uint16_t>(data[0]) |
               static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
    }

    constexpr uint32_t le32(const uint8_t* data) {
        return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
               (static_cast<uint32_t>(data[2]) << 16) |
               (static_cast<uint32_t>(data[3]) << 24);
    }

    constexpr void store_le16(uint8_t* data, uint16_t value) {
        data[0] = static_cast<uint8_t>(value);
        data[1] = static_cast<uint8_t>(value >> 8);
    }

    constexpr void store_le32(uint8_t* data, uint32_t value) {
        data[0] = static_cast<uint8_t>(value);
        data[1] = static_cast<uint8_t>(value >> 8);
        data[2] = static_cast<uint8_t>(value >> 16);
        data[3] = static_cast<uint8_t>(value >> 24);
    }

    class register_file final {
        volatile uint8_t* _base = nullptr;
        uint64_t          _size = 0;

        bool contains(register_offset offset, uint64_t width) const {
            const uint64_t value = static_cast<uint16_t>(offset);

            return _base != nullptr && value <= _size && width <= _size - value;
        }

        public:
        register_file() = default;

        register_file(volatile uint8_t* base, uint64_t size) : _base(base), _size(size) {}

        bool valid() const {
            return _base != nullptr && _size >= 0x100;
        }

        uint8_t read8(register_offset offset) const {
            return contains(offset, 1) ? _base[static_cast<uint16_t>(offset)] : 0xff;
        }

        uint16_t read16(register_offset offset) const {
            if (!contains(offset, 2))
                return 0xffff;

            const uint16_t address = static_cast<uint16_t>(offset);
            const uint8_t  bytes[] = {_base[address], _base[address + 1]};

            return le16(bytes);
        }

        uint32_t read32(register_offset offset) const {
            if (!contains(offset, 4))
                return 0xffffffffu;

            const uint16_t address = static_cast<uint16_t>(offset);
            const uint8_t  bytes[] = {_base[address], _base[address + 1],
                                      _base[address + 2], _base[address + 3]};

            return le32(bytes);
        }

        void write8(register_offset offset, uint8_t value) const {
            if (contains(offset, 1))
                _base[static_cast<uint16_t>(offset)] = value;
        }

        void write16(register_offset offset, uint16_t value) const {
            if (!contains(offset, 2))
                return;

            const uint16_t address = static_cast<uint16_t>(offset);

            _base[address]     = static_cast<uint8_t>(value);
            _base[address + 1] = static_cast<uint8_t>(value >> 8);
        }

        void write32(register_offset offset, uint32_t value) const {
            if (!contains(offset, 4))
                return;

            const uint16_t address = static_cast<uint16_t>(offset);

            _base[address]     = static_cast<uint8_t>(value);
            _base[address + 1] = static_cast<uint8_t>(value >> 8);
            _base[address + 2] = static_cast<uint8_t>(value >> 16);
            _base[address + 3] = static_cast<uint8_t>(value >> 24);
        }
    };

    inline bool pci_match(const astia::hw::pci_device* device) {
        return device != nullptr && device->vendor_id == pci_vendor_id &&
               device->device_id == pci_device_id &&
               device->class_code == pci_class_network &&
               device->subclass == pci_subclass_ethernet &&
               device->programming_interface == pci_programming_interface &&
               device->bar_count != 0;
    }

    struct packet_segment {
        const uint8_t* data   = nullptr;
        uint16_t       length = 0;
    };

    struct packet_view {
        packet_segment first{};
        packet_segment second{};
        uint16_t       length      = 0;
        uint16_t       wire_length = 0;
        uint16_t       ring_offset = 0;
        uint16_t       next_offset = 0;
        uint16_t       status      = 0;

        constexpr unsigned segment_count() const {
            return second.length == 0 ? 1u : 2u;
        }
    };

    enum class rx_inspect_result : uint8_t { empty, ready, malformed };

    struct rx_packet {
        packet_view view{};
    };

    inline uint16_t ring_offset(uint32_t value) {
        return static_cast<uint16_t>(value & (rx_ring_size - 1u));
    }

    inline uint16_t capr_for_cursor(uint16_t cursor) {
        return ring_offset(static_cast<uint32_t>(cursor) + rx_ring_size - 16u);
    }

    inline uint8_t ring_byte(const uint8_t* ring, uint16_t offset) {
        return ring[ring_offset(offset)];
    }

    inline rx_inspect_result inspect_rx_packet(const uint8_t* ring, uint16_t cursor,
                                               rx_packet& result) {
        result = {};

        if (ring == nullptr)
            return rx_inspect_result::malformed;

        const uint16_t status = static_cast<uint16_t>(ring_byte(ring, cursor)) |
                                static_cast<uint16_t>(ring_byte(ring, cursor + 1) << 8);
        const uint16_t wire_length =
            static_cast<uint16_t>(ring_byte(ring, cursor + 2)) |
            static_cast<uint16_t>(ring_byte(ring, cursor + 3) << 8);

        if (status == 0 && wire_length == 0)
            return rx_inspect_result::empty;

        if ((status & rx_status_ok) == 0 || wire_length < rx_crc_size ||
            wire_length > rx_max_frame_with_crc)
            return rx_inspect_result::malformed;

        const uint16_t payload_offset =
            ring_offset(static_cast<uint32_t>(cursor) + rx_header_size);
        const uint16_t payload_length = static_cast<uint16_t>(wire_length - rx_crc_size);
        const uint16_t first_length =
            static_cast<uint16_t>(payload_length < rx_ring_size - payload_offset
                                      ? payload_length
                                      : rx_ring_size - payload_offset);

        result.view.first  = {ring + payload_offset, first_length};
        result.view.second = {ring, static_cast<uint16_t>(payload_length - first_length)};
        result.view.length = payload_length;
        result.view.wire_length = wire_length;
        result.view.ring_offset = cursor;
        result.view.next_offset = ring_offset(static_cast<uint32_t>(cursor) +
                                              rx_header_size + wire_length + 3u);
        result.view.status      = status;

        return rx_inspect_result::ready;
    }

    class rx_ring final {
        uint8_t* _data   = nullptr;
        uint16_t _cursor = 0;

        public:
        void initialize(uint8_t* data = nullptr) {
            _data   = data;
            _cursor = 0;
        }

        void reset(uint8_t* data, uint16_t cursor = 0) {
            _data   = data;
            _cursor = ring_offset(cursor);
        }

        uint16_t cursor() const {
            return _cursor;
        }

        bool pending(uint16_t hardware_cursor) const {
            return _data != nullptr && _cursor != ring_offset(hardware_cursor);
        }

        rx_inspect_result acquire(rx_packet& packet) const {
            return inspect_rx_packet(_data, _cursor, packet);
        }

        bool recycle(const rx_packet& packet) {
            if (_data == nullptr || packet.view.ring_offset != _cursor ||
                packet.view.next_offset == _cursor)
                return false;

            _cursor = packet.view.next_offset;

            return true;
        }

        bool recycle_to(uint16_t next) {
            const uint16_t normalized = ring_offset(next);

            if (_data == nullptr || normalized == _cursor)
                return false;

            _cursor = normalized;

            return true;
        }
    };

    using rx_action            = astia::net::rx_action;
    using rx_irq_callback      = astia::net::rx_irq_callback;
    using rx_deferred_callback = astia::net::rx_deferred_callback;
    using tx_request           = astia::net::tx_request;

    struct tx_completion {
        tx_request request{};
        int        status = 0;
    };

    enum class tx_slot_state : uint8_t { free, device, completion };

    class tx_ring final {
        struct slot {
            tx_request    request{};
            tx_slot_state state = tx_slot_state::free;
        };

        slot _slots[tx_descriptor_count]{};

        public:
        void initialize() {
            for (slot& value : _slots) {
                value = {};
            }
        }

        int available() const {
            for (uint32_t index = 0; index < tx_descriptor_count; ++index) {
                if (_slots[index].state == tx_slot_state::free)
                    return static_cast<int>(index);
            }

            return -1;
        }

        bool claim(uint32_t index, const tx_request& request) {
            if (index >= tx_descriptor_count ||
                _slots[index].state != tx_slot_state::free)
                return false;

            _slots[index].request = request;
            _slots[index].state   = tx_slot_state::device;

            return true;
        }

        const tx_request* request(uint32_t index) const {
            return index < tx_descriptor_count &&
                           _slots[index].state != tx_slot_state::free
                       ? &_slots[index].request
                       : nullptr;
        }

        tx_slot_state state(uint32_t index) const {
            return index < tx_descriptor_count ? _slots[index].state
                                               : tx_slot_state::free;
        }

        bool complete(uint32_t index, tx_request& result) {
            if (index >= tx_descriptor_count ||
                _slots[index].state != tx_slot_state::device)
                return false;

            result              = _slots[index].request;
            _slots[index].state = tx_slot_state::completion;

            return true;
        }

        bool restore(uint32_t index) {
            if (index >= tx_descriptor_count ||
                _slots[index].state != tx_slot_state::completion)
                return false;

            _slots[index].state = tx_slot_state::device;

            return true;
        }

        bool release(uint32_t index) {
            if (index >= tx_descriptor_count ||
                _slots[index].state == tx_slot_state::free)
                return false;

            _slots[index] = {};

            return true;
        }
    };

    template <typename T, uint32_t Capacity>
    using bounded_mpsc_queue = astia::net::bounded_mpsc_queue<T, Capacity>;

    template <typename T, uint32_t Capacity>
    using bounded_spsc_queue = astia::net::bounded_spsc_queue<T, Capacity>;

    enum class device_state : uint8_t {
        detached,
        probed_no_irq,
        running,
        stopping,
        reset,
    };
} // namespace astia::rtl8139
