#pragma once

#include <astia/module_abi_core.h>
#include <astia/module_abi_pci.h>

#include <stdint.h>

namespace astia::rtl8139 {

constexpr uint16_t pci_vendor_id = 0x10ec;
constexpr uint16_t pci_device_id = 0x8139;
constexpr uint8_t pci_class_network = 0x02;
constexpr uint8_t pci_subclass_ethernet = 0x00;
constexpr uint8_t pci_programming_interface = 0x00;

constexpr uint32_t rx_ring_size = 8u * 1024u;
constexpr uint32_t rx_ring_allocation = rx_ring_size + 16u;
constexpr uint32_t rx_ring_pages =
    (rx_ring_allocation + ASTIA_PAGE_SIZE - 1u) / ASTIA_PAGE_SIZE;
constexpr uint32_t rx_header_size = 4;
constexpr uint32_t rx_crc_size = 4;
constexpr uint32_t rx_max_frame_with_crc = 4096;
constexpr uint32_t tx_descriptor_count = 4;
constexpr uint32_t tx_max_frame = 1792;
constexpr uint32_t tx_queue_depth = 16;
constexpr uint32_t tx_completion_depth = tx_descriptor_count;
constexpr uint32_t rx_deferred_depth = 2;

enum class register_offset : uint16_t {
    id0 = 0x00,
    multicast0 = 0x08,
    tx_status0 = 0x10,
    tx_address0 = 0x20,
    rx_buffer_start = 0x30,
    command = 0x37,
    current_address = 0x38,
    current_buffer = 0x3a,
    interrupt_mask = 0x3c,
    interrupt_status = 0x3e,
    tx_config = 0x40,
    rx_config = 0x44,
    c_plus_command = 0xe0,
};

constexpr uint8_t command_reset = 1u << 4;
constexpr uint8_t command_receiver_enable = 1u << 3;
constexpr uint8_t command_transmitter_enable = 1u << 2;
constexpr uint8_t command_buffer_empty = 1u << 0;

constexpr uint16_t interrupt_rx_ok = 1u << 0;
constexpr uint16_t interrupt_rx_error = 1u << 1;
constexpr uint16_t interrupt_tx_ok = 1u << 2;
constexpr uint16_t interrupt_tx_error = 1u << 3;
constexpr uint16_t interrupt_rx_overflow = 1u << 4;
constexpr uint16_t interrupt_link_change = 1u << 5;
constexpr uint16_t interrupt_fifo_overflow = 1u << 6;
constexpr uint16_t interrupt_rx_tx = interrupt_rx_ok | interrupt_rx_error | interrupt_tx_ok |
                                     interrupt_tx_error | interrupt_rx_overflow |
                                     interrupt_link_change | interrupt_fifo_overflow;

constexpr uint16_t rx_status_ok = 1u << 0;
constexpr uint16_t rx_status_frame_alignment = 1u << 1;
constexpr uint16_t rx_status_crc = 1u << 2;
constexpr uint16_t rx_status_long = 1u << 3;
constexpr uint16_t rx_status_runt = 1u << 4;

constexpr uint32_t tx_status_ok = 1u << 15;
constexpr uint32_t tx_status_underrun = 1u << 14;
constexpr uint32_t tx_status_own = 1u << 13;
constexpr uint32_t tx_status_abort = 1u << 30;

constexpr uint16_t le16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
}

constexpr uint32_t le32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
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
    volatile uint8_t* base_ = nullptr;
    uint64_t size_ = 0;

    bool contains(register_offset offset, uint64_t width) const {
        const uint64_t value = static_cast<uint16_t>(offset);
        return base_ != nullptr && value <= size_ && width <= size_ - value;
    }

    public:
    register_file() = default;
    register_file(volatile uint8_t* base, uint64_t size) : base_(base), size_(size) {}

    bool valid() const { return base_ != nullptr && size_ >= 0x100; }

    uint8_t read8(register_offset offset) const {
        return contains(offset, 1) ? base_[static_cast<uint16_t>(offset)] : 0xff;
    }

    uint16_t read16(register_offset offset) const {
        if (!contains(offset, 2))
            return 0xffff;
        const uint16_t address = static_cast<uint16_t>(offset);
        const uint8_t bytes[] = {base_[address], base_[address + 1]};
        return le16(bytes);
    }

    uint32_t read32(register_offset offset) const {
        if (!contains(offset, 4))
            return 0xffffffffu;
        const uint16_t address = static_cast<uint16_t>(offset);
        const uint8_t bytes[] = {base_[address], base_[address + 1], base_[address + 2],
                                 base_[address + 3]};
        return le32(bytes);
    }

    void write8(register_offset offset, uint8_t value) const {
        if (contains(offset, 1))
            base_[static_cast<uint16_t>(offset)] = value;
    }

    void write16(register_offset offset, uint16_t value) const {
        if (!contains(offset, 2))
            return;
        const uint16_t address = static_cast<uint16_t>(offset);
        base_[address] = static_cast<uint8_t>(value);
        base_[address + 1] = static_cast<uint8_t>(value >> 8);
    }

    void write32(register_offset offset, uint32_t value) const {
        if (!contains(offset, 4))
            return;
        const uint16_t address = static_cast<uint16_t>(offset);
        base_[address] = static_cast<uint8_t>(value);
        base_[address + 1] = static_cast<uint8_t>(value >> 8);
        base_[address + 2] = static_cast<uint8_t>(value >> 16);
        base_[address + 3] = static_cast<uint8_t>(value >> 24);
    }
};

inline bool pci_match(const astia_module_pci_device* device) {
    return device != nullptr && device->vendor_id == pci_vendor_id &&
           device->device_id == pci_device_id && device->class_code == pci_class_network &&
           device->subclass == pci_subclass_ethernet &&
           device->programming_interface == pci_programming_interface &&
           device->bar_count != 0;
}

struct packet_segment {
    const uint8_t* data = nullptr;
    uint16_t length = 0;
};

struct packet_view {
    packet_segment first{};
    packet_segment second{};
    uint16_t length = 0;
    uint16_t wire_length = 0;
    uint16_t ring_offset = 0;
    uint16_t next_offset = 0;
    uint16_t status = 0;

    constexpr unsigned segment_count() const { return second.length == 0 ? 1u : 2u; }
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
    const uint16_t wire_length = static_cast<uint16_t>(ring_byte(ring, cursor + 2)) |
                                 static_cast<uint16_t>(ring_byte(ring, cursor + 3) << 8);
    if (status == 0 && wire_length == 0)
        return rx_inspect_result::empty;
    if ((status & rx_status_ok) == 0 || wire_length < rx_crc_size ||
        wire_length > rx_max_frame_with_crc)
        return rx_inspect_result::malformed;
    const uint16_t payload_offset = ring_offset(static_cast<uint32_t>(cursor) + rx_header_size);
    const uint16_t payload_length = static_cast<uint16_t>(wire_length - rx_crc_size);
    const uint16_t first_length = static_cast<uint16_t>(
        payload_length < rx_ring_size - payload_offset ? payload_length
                                                       : rx_ring_size - payload_offset);
    result.view.first = {ring + payload_offset, first_length};
    result.view.second = {ring, static_cast<uint16_t>(payload_length - first_length)};
    result.view.length = payload_length;
    result.view.wire_length = wire_length;
    result.view.ring_offset = cursor;
    result.view.next_offset = ring_offset(static_cast<uint32_t>(cursor) + rx_header_size +
                                          wire_length + 3u);
    result.view.status = status;
    return rx_inspect_result::ready;
}

class rx_ring final {
    uint8_t* data_ = nullptr;
    uint16_t cursor_ = 0;

    public:
    rx_ring() = default;
    explicit rx_ring(uint8_t* data) : data_(data) {}

    void reset(uint8_t* data) {
        data_ = data;
        cursor_ = 0;
    }

    void reset(uint8_t* data, uint16_t cursor) {
        data_ = data;
        cursor_ = ring_offset(cursor);
    }

    uint8_t* data() const { return data_; }
    uint16_t cursor() const { return cursor_; }
    bool pending(uint16_t hardware_cursor) const {
        return cursor_ != ring_offset(hardware_cursor);
    }

    rx_inspect_result acquire(rx_packet& packet) const {
        return inspect_rx_packet(data_, cursor_, packet);
    }

    bool recycle(const rx_packet& packet) {
        if (data_ == nullptr || packet.view.ring_offset != cursor_ ||
            packet.view.next_offset == cursor_)
            return false;
        cursor_ = packet.view.next_offset;
        return true;
    }

    bool recycle_to(uint16_t next) {
        const uint16_t normalized = ring_offset(next);
        if (data_ == nullptr || normalized == cursor_)
            return false;
        cursor_ = normalized;
        return true;
    }
};

enum class rx_action : uint8_t { recycle, defer, drop };
using rx_irq_callback = rx_action (*)(void*, const packet_view&);
using rx_deferred_callback = void (*)(void*, const packet_view&);

struct tx_request;
using tx_reclaim_callback = void (*)(void*, const tx_request&, int);

struct tx_request {
    const uint8_t* data = nullptr;
    uint64_t physical = 0;
    uint16_t length = 0;
    void* cookie = nullptr;
    tx_reclaim_callback reclaim = nullptr;
};

struct tx_completion {
    tx_request request{};
    int status = 0;
    uint8_t descriptor = 0xff;
};

enum class tx_slot_state : uint8_t { free, device, completion };

class tx_ring final {
    struct slot {
        tx_request request{};
        tx_slot_state state = tx_slot_state::free;
    };

    slot slots_[tx_descriptor_count]{};

    public:
    int available() const {
        for (uint32_t index = 0; index < tx_descriptor_count; ++index)
            if (slots_[index].state == tx_slot_state::free)
                return static_cast<int>(index);
        return -1;
    }

    bool claim(uint32_t index, const tx_request& request) {
        if (index >= tx_descriptor_count || slots_[index].state != tx_slot_state::free)
            return false;
        slots_[index].request = request;
        slots_[index].state = tx_slot_state::device;
        return true;
    }

    const tx_request* request(uint32_t index) const {
        return index < tx_descriptor_count && slots_[index].state != tx_slot_state::free
                   ? &slots_[index].request
                   : nullptr;
    }

    tx_slot_state state(uint32_t index) const {
        return index < tx_descriptor_count ? slots_[index].state : tx_slot_state::free;
    }

    bool complete(uint32_t index, int status, tx_completion& completion) {
        if (index >= tx_descriptor_count || slots_[index].state != tx_slot_state::device)
            return false;
        completion.request = slots_[index].request;
        completion.status = status;
        completion.descriptor = static_cast<uint8_t>(index);
        slots_[index].state = tx_slot_state::completion;
        return true;
    }

    bool restore(uint32_t index) {
        if (index >= tx_descriptor_count || slots_[index].state != tx_slot_state::completion)
            return false;
        slots_[index].state = tx_slot_state::device;
        return true;
    }

    bool release(uint32_t index) {
        if (index >= tx_descriptor_count || slots_[index].state != tx_slot_state::completion)
            return false;
        slots_[index] = {};
        return true;
    }
};

class atomic_u32 final {
    volatile uint32_t value_ = 0;

    public:
    atomic_u32() = default;
    explicit atomic_u32(uint32_t value) : value_(value) {}
    atomic_u32(const atomic_u32&) = delete;
    atomic_u32& operator=(const atomic_u32&) = delete;

    uint32_t load(int order = __ATOMIC_RELAXED) const {
        return __atomic_load_n(&value_, order);
    }

    void store(uint32_t value, int order = __ATOMIC_RELAXED) {
        __atomic_store_n(&value_, value, order);
    }

    bool compare_exchange(uint32_t& expected, uint32_t desired, int success = __ATOMIC_RELAXED,
                          int failure = __ATOMIC_RELAXED) {
        return __atomic_compare_exchange_n(&value_, &expected, desired, false, success,
                                           failure);
    }
};

template <typename T, uint32_t Capacity>
class bounded_mpsc_queue final {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1u)) == 0,
                  "queue capacity must be a power of two");

    struct cell {
        atomic_u32 sequence;
        T value{};
    };

    cell cells_[Capacity]{};
    atomic_u32 enqueue_position_;
    atomic_u32 dequeue_position_;

    public:
    bounded_mpsc_queue() {
        for (uint32_t i = 0; i < Capacity; ++i)
            cells_[i].sequence.store(i);
    }

    bounded_mpsc_queue(const bounded_mpsc_queue&) = delete;
    bounded_mpsc_queue& operator=(const bounded_mpsc_queue&) = delete;

    bool push(const T& value) {
        uint32_t position = enqueue_position_.load();
        for (;;) {
            cell& slot = cells_[position & (Capacity - 1u)];
            const uint32_t sequence = slot.sequence.load(__ATOMIC_ACQUIRE);
            const int32_t difference = static_cast<int32_t>(sequence - position);
            if (difference == 0) {
                if (enqueue_position_.compare_exchange(position, position + 1u)) {
                    slot.value = value;
                    slot.sequence.store(position + 1u, __ATOMIC_RELEASE);
                    return true;
                }
            } else if (difference < 0) {
                return false;
            } else {
                position = enqueue_position_.load();
            }
        }
    }

    bool pop(T& value) {
        const uint32_t position = dequeue_position_.load();
        cell& slot = cells_[position & (Capacity - 1u)];
        const uint32_t sequence = slot.sequence.load(__ATOMIC_ACQUIRE);
        if (static_cast<int32_t>(sequence - (position + 1u)) < 0)
            return false;
        value = slot.value;
        slot.sequence.store(position + Capacity, __ATOMIC_RELEASE);
        dequeue_position_.store(position + 1u);
        return true;
    }
};

template <typename T, uint32_t Capacity>
class bounded_spsc_queue final {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1u)) == 0,
                  "queue capacity must be a power of two");

    T values_[Capacity]{};
    atomic_u32 head_;
    atomic_u32 tail_;

    public:
    bool push(const T& value) {
        const uint32_t head = head_.load();
        const uint32_t tail = tail_.load(__ATOMIC_ACQUIRE);
        if (head - tail == Capacity)
            return false;
        values_[head & (Capacity - 1u)] = value;
        head_.store(head + 1u, __ATOMIC_RELEASE);
        return true;
    }

    bool pop(T& value) {
        const uint32_t tail = tail_.load();
        const uint32_t head = head_.load(__ATOMIC_ACQUIRE);
        if (tail == head)
            return false;
        value = values_[tail & (Capacity - 1u)];
        tail_.store(tail + 1u, __ATOMIC_RELEASE);
        return true;
    }
};

struct net_device {
    const char* name = nullptr;
    uint8_t mac[6]{};
    uint16_t mtu = 1500;
    bool running = false;
    void* context = nullptr;
    rx_irq_callback rx_irq = nullptr;
    rx_deferred_callback rx_deferred = nullptr;
};

enum class device_state : uint8_t {
    detached,
    probed_no_irq,
    running,
    stopping,
    reset,
};

} // namespace astia::rtl8139
