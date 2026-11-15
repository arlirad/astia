#pragma once

#include <stdint.h>

namespace astia::usb {

constexpr unsigned max_configurations = 8;
constexpr unsigned max_interfaces = 16;
constexpr unsigned max_endpoints = 32;
constexpr unsigned max_drivers = 16;
constexpr unsigned max_devices = 32;

constexpr uint8_t descriptor_device = 1;
constexpr uint8_t descriptor_configuration = 2;
constexpr uint8_t descriptor_string = 3;
constexpr uint8_t descriptor_interface = 4;
constexpr uint8_t descriptor_endpoint = 5;
constexpr uint8_t descriptor_other_speed_configuration = 7;
constexpr uint8_t descriptor_bos = 15;
constexpr uint8_t descriptor_device_capability = 16;
constexpr uint8_t descriptor_ss_endpoint_companion = 48;
constexpr uint8_t descriptor_ssp_isochronous_companion = 49;

enum class parse_error : uint8_t {
    none,
    null_data,
    truncated,
    bad_length,
    wrong_type,
    bad_value,
    too_many_interfaces,
    too_many_endpoints,
    missing_interface,
    missing_companion,
    duplicate_endpoint,
};

struct parse_result {
    parse_error error = parse_error::none;
    uint32_t offset = 0;

    constexpr explicit operator bool() const {
        return error == parse_error::none;
    }
};

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

struct descriptor_view {
    const uint8_t* data = nullptr;
    uint8_t length = 0;
    uint8_t type = 0;
};

inline parse_result descriptor_at(const uint8_t* data, uint32_t available,
                                  descriptor_view& result) {
    result = {};
    if (data == nullptr)
        return {parse_error::null_data, 0};
    if (available < 2)
        return {parse_error::truncated, available};
    result.length = data[0];
    result.type = data[1];
    if (result.length < 2)
        return {parse_error::bad_length, 0};
    if (result.length > available)
        return {parse_error::truncated, 0};
    result.data = data;
    return {};
}

enum class usb_speed : uint8_t { low, full, high, super, super_plus };

struct device_descriptor {
    uint16_t usb_version = 0;
    uint8_t device_class = 0;
    uint8_t device_subclass = 0;
    uint8_t device_protocol = 0;
    uint8_t max_packet_size0 = 0;
    uint16_t vendor_id = 0;
    uint16_t product_id = 0;
    uint16_t device_version = 0;
    uint8_t manufacturer = 0;
    uint8_t product = 0;
    uint8_t serial = 0;
    uint8_t configurations = 0;
};

inline bool valid_packet_size0(uint8_t value, usb_speed speed) {
    if (speed == usb_speed::super || speed == usb_speed::super_plus)
        return value == 9;
    return value == 8 || value == 16 || value == 32 || value == 64;
}

inline parse_result parse_device_descriptor(const uint8_t* data, uint32_t available,
                                            device_descriptor& result,
                                            usb_speed speed = usb_speed::full) {
    result = {};
    descriptor_view view{};
    parse_result status = descriptor_at(data, available, view);
    if (!status)
        return status;
    if (view.type != descriptor_device)
        return {parse_error::wrong_type, 1};
    if (view.length < 18)
        return {parse_error::bad_length, 0};
    if (data[4] == 0 && (data[5] != 0 || data[6] != 0))
        return {parse_error::bad_value, 4};
    if (!valid_packet_size0(data[7], speed))
        return {parse_error::bad_value, 7};
    if (data[17] == 0)
        return {parse_error::bad_value, 17};
    result.usb_version = le16(data + 2);
    result.device_class = data[4];
    result.device_subclass = data[5];
    result.device_protocol = data[6];
    result.max_packet_size0 = data[7];
    result.vendor_id = le16(data + 8);
    result.product_id = le16(data + 10);
    result.device_version = le16(data + 12);
    result.manufacturer = data[14];
    result.product = data[15];
    result.serial = data[16];
    result.configurations = data[17];
    return {};
}

struct endpoint_companion {
    bool present = false;
    uint8_t max_burst = 0;
    uint8_t attributes = 0;
    uint16_t bytes_per_interval = 0;
    uint32_t ssp_bytes_per_interval = 0;
};

enum class transfer_type : uint8_t { control, isochronous, bulk, interrupt };

struct endpoint_model {
    uint8_t address = 0;
    uint8_t number = 0;
    bool in = false;
    transfer_type type = transfer_type::control;
    uint8_t attributes = 0;
    uint16_t max_packet_size = 0;
    uint16_t transactions = 1;
    uint8_t interval = 0;
    endpoint_companion companion{};
};

struct interface_model {
    uint8_t number = 0;
    uint8_t alternate = 0;
    uint8_t interface_class = 0;
    uint8_t interface_subclass = 0;
    uint8_t interface_protocol = 0;
    uint8_t string_index = 0;
    uint8_t endpoint_count = 0;
    endpoint_model endpoints[max_endpoints]{};
};

struct configuration_model {
    uint16_t total_length = 0;
    uint8_t interface_count = 0;
    uint8_t value = 0;
    uint8_t string_index = 0;
    uint8_t attributes = 0;
    uint8_t max_power_units = 0;
    uint8_t interface_descriptors = 0;
    uint8_t endpoint_descriptors = 0;
    interface_model interfaces[max_interfaces]{};
};

inline bool endpoint_attributes_valid(uint8_t attributes, transfer_type type) {
    if (type == transfer_type::isochronous)
        return (attributes & 0xc0u) == 0;
    if (type == transfer_type::interrupt)
        return (attributes & 0x0cu) == 0;
    return (attributes & 0x3cu) == 0;
}

inline parse_result parse_endpoint_descriptor(const uint8_t* data, uint32_t available,
                                              endpoint_model& result) {
    result = {};
    descriptor_view view{};
    parse_result status = descriptor_at(data, available, view);
    if (!status)
        return status;
    if (view.type != descriptor_endpoint)
        return {parse_error::wrong_type, 1};
    if (view.length < 7)
        return {parse_error::bad_length, 0};
    const uint8_t address = data[2];
    if ((address & 0x70u) != 0 || (address & 0x0fu) == 0)
        return {parse_error::bad_value, 2};
    const uint8_t raw_type = data[3] & 3u;
    const transfer_type type = static_cast<transfer_type>(raw_type);
    if (type == transfer_type::control)
        return {parse_error::bad_value, 3};
    if (!endpoint_attributes_valid(data[3], type))
        return {parse_error::bad_value, 3};
    const uint16_t packet = static_cast<uint16_t>(le16(data + 4) & 0x07ffu);
    const uint16_t transactions = static_cast<uint16_t>(((le16(data + 4) >> 11) & 3u) + 1u);
    if (packet == 0 || packet > 1024 || transactions == 4 ||
        ((type == transfer_type::control || type == transfer_type::bulk) &&
         transactions != 1))
        return {parse_error::bad_value, 4};
    result.address = address;
    result.number = static_cast<uint8_t>(address & 0x0fu);
    result.in = (address & 0x80u) != 0;
    result.type = type;
    result.attributes = data[3];
    result.max_packet_size = packet;
    result.transactions = transactions;
    result.interval = data[6];
    return {};
}

inline parse_result parse_endpoint_companion(const uint8_t* data, uint32_t available,
                                             endpoint_model& endpoint) {
    descriptor_view view{};
    parse_result status = descriptor_at(data, available, view);
    if (!status)
        return status;
    if (view.type != descriptor_ss_endpoint_companion)
        return {parse_error::wrong_type, 1};
    if (view.length < 6)
        return {parse_error::bad_length, 0};
    if (data[2] > 15)
        return {parse_error::bad_value, 2};
    const uint8_t attributes = data[3];
    if (endpoint.type == transfer_type::control && (data[2] != 0 || attributes != 0))
        return {parse_error::bad_value, 2};
    if (endpoint.type == transfer_type::bulk && (attributes & 0xe0u) != 0)
        return {parse_error::bad_value, 3};
    if (endpoint.type == transfer_type::interrupt && attributes != 0)
        return {parse_error::bad_value, 3};
    if (endpoint.type == transfer_type::isochronous && (attributes & 0x7cu) != 0)
        return {parse_error::bad_value, 3};
    endpoint.companion.present = true;
    endpoint.companion.max_burst = data[2];
    endpoint.companion.attributes = attributes;
    endpoint.companion.bytes_per_interval = le16(data + 4);
    return {};
}

inline bool contains_endpoint(const interface_model& interface, uint8_t address) {
    for (unsigned i = 0; i < interface.endpoint_count; ++i)
        if (interface.endpoints[i].address == address)
            return true;
    return false;
}

inline parse_result parse_configuration_descriptor(const uint8_t* data, uint32_t available,
                                                   configuration_model& result,
                                                   bool superspeed = false) {
    result = {};
    if (data == nullptr)
        return {parse_error::null_data, 0};
    if (available < 9)
        return {parse_error::truncated, available};
    descriptor_view header{};
    parse_result status = descriptor_at(data, available, header);
    if (!status)
        return status;
    if (header.type != descriptor_configuration &&
        header.type != descriptor_other_speed_configuration)
        return {parse_error::wrong_type, 1};
    if (header.length < 9)
        return {parse_error::bad_length, 0};
    const uint16_t total = le16(data + 2);
    const uint8_t interfaces = data[4];
    if (total < 9 || total > available || interfaces == 0 || interfaces > max_interfaces)
        return {parse_error::bad_value, 2};
    if ((data[7] & 0x80u) == 0 || (data[7] & 0x1fu) != 0 || data[5] == 0)
        return {parse_error::bad_value, 5};
    result.total_length = total;
    result.interface_count = interfaces;
    result.value = data[5];
    result.string_index = data[6];
    result.attributes = data[7];
    result.max_power_units = data[8];
    uint32_t offset = 0;
    int current = -1;
    unsigned unique_interfaces = 0;
    bool seen_numbers[max_interfaces]{};
    uint8_t actual_endpoints[max_interfaces]{};
    while (offset < total) {
        descriptor_view view{};
        status = descriptor_at(data + offset, total - offset, view);
        if (!status)
            return {status.error, offset + status.offset};
        if (offset == 0 &&
            ((view.type != descriptor_configuration &&
              view.type != descriptor_other_speed_configuration) || view.length < 9))
            return {parse_error::wrong_type, offset};
        if (view.type == descriptor_interface) {
            if (current >= 0 && actual_endpoints[current] !=
                                    result.interfaces[current].endpoint_count)
                return {parse_error::bad_value, offset};
            if (view.length < 9 || result.interface_descriptors == max_interfaces)
                return {parse_error::too_many_interfaces, offset};
            const uint8_t number = data[offset + 2];
            if (number >= interfaces)
                return {parse_error::bad_value, offset + 2};
            if (data[offset + 5] == 0 &&
                (data[offset + 6] != 0 || data[offset + 7] != 0))
                return {parse_error::bad_value, offset + 5};
            interface_model& item = result.interfaces[result.interface_descriptors++];
            item.number = number;
            item.alternate = data[offset + 3];
            item.interface_class = data[offset + 5];
            item.interface_subclass = data[offset + 6];
            item.interface_protocol = data[offset + 7];
            item.string_index = data[offset + 8];
            item.endpoint_count = data[offset + 4];
            if (item.endpoint_count > max_endpoints)
                return {parse_error::too_many_endpoints, offset + 4};
            current = static_cast<int>(result.interface_descriptors - 1);
            if (!seen_numbers[number]) {
                seen_numbers[number] = true;
                ++unique_interfaces;
            }
        } else if (view.type == descriptor_endpoint) {
            if (current < 0)
                return {parse_error::missing_interface, offset};
            interface_model& item = result.interfaces[current];
            const unsigned endpoint_index = actual_endpoints[current];
            if (item.endpoint_count == 0 || item.endpoint_count <= endpoint_index ||
                result.endpoint_descriptors >= max_endpoints)
                return {parse_error::bad_value, offset};
            endpoint_model endpoint{};
            status = parse_endpoint_descriptor(data + offset, total - offset, endpoint);
            if (!status)
                return {status.error, offset + status.offset};
            if (contains_endpoint(item, endpoint.address))
                return {parse_error::duplicate_endpoint, offset + 2};
            item.endpoints[endpoint_index] = endpoint;
            ++actual_endpoints[current];
            ++result.endpoint_descriptors;
            if (superspeed) {
                const uint32_t next = offset + view.length;
                descriptor_view companion{};
                status = descriptor_at(data + next, total - next, companion);
                if (!status || companion.type != descriptor_ss_endpoint_companion)
                    return {parse_error::missing_companion, next};
                status = parse_endpoint_companion(data + next, total - next,
                                                  item.endpoints[endpoint_index]);
                if (!status)
                    return {status.error, next + status.offset};
                offset = next + companion.length;
                continue;
            }
        } else if (view.type == descriptor_ss_endpoint_companion && superspeed) {
            return {parse_error::bad_value, offset};
        }
        offset += view.length;
    }
    if (offset != total || unique_interfaces != interfaces)
        return {parse_error::bad_value, offset};
    for (unsigned i = 0; i < result.interface_descriptors; ++i)
        if (actual_endpoints[i] != result.interfaces[i].endpoint_count)
            return {parse_error::bad_value, i};
    return {};
}

struct device_model {
    bool present = false;
    uint8_t address = 0;
    usb_speed speed = usb_speed::full;
    device_descriptor descriptor{};
    configuration_model configurations[max_configurations]{};
    uint8_t configuration_count = 0;
};

struct bus_model {
    device_model devices[max_devices]{};

    bool attach(unsigned slot, usb_speed speed, const device_descriptor& descriptor) {
        if (slot >= max_devices || devices[slot].present)
            return false;
        devices[slot] = {};
        devices[slot].present = true;
        devices[slot].speed = speed;
        devices[slot].descriptor = descriptor;
        return true;
    }

    bool detach(unsigned slot) {
        if (slot >= max_devices || !devices[slot].present)
            return false;
        devices[slot] = {};
        return true;
    }
};

enum class enumeration_state : uint8_t {
    detached,
    attached,
    powered,
    default_state,
    addressed,
    configured,
    suspended,
    unsupported,
    error,
};

enum class enumeration_action : uint8_t {
    none,
    power,
    port_reset,
    get_device_descriptor,
    set_address,
    get_configuration,
    set_configuration,
};

enum class enumeration_event : uint8_t {
    attach,
    power_ready,
    port_reset_complete,
    device_descriptor_complete,
    address_complete,
    configuration_complete,
    set_configuration_complete,
    suspend,
    resume,
    detach,
    failure,
    unsupported,
};

class enumeration_machine {
    enumeration_state state_ = enumeration_state::detached;
    enumeration_action action_ = enumeration_action::none;

    public:
    enumeration_state state() const { return state_; }
    enumeration_action action() const { return action_; }

    bool event(enumeration_event value) {
        switch (value) {
        case enumeration_event::attach:
            if (state_ != enumeration_state::detached)
                return false;
            state_ = enumeration_state::attached;
            action_ = enumeration_action::power;
            return true;
        case enumeration_event::power_ready:
            if (state_ != enumeration_state::attached)
                return false;
            state_ = enumeration_state::powered;
            action_ = enumeration_action::port_reset;
            return true;
        case enumeration_event::port_reset_complete:
            if (state_ != enumeration_state::powered)
                return false;
            state_ = enumeration_state::default_state;
            action_ = enumeration_action::get_device_descriptor;
            return true;
        case enumeration_event::device_descriptor_complete:
            if (state_ != enumeration_state::default_state)
                return false;
            action_ = enumeration_action::set_address;
            return true;
        case enumeration_event::address_complete:
            if (state_ != enumeration_state::default_state)
                return false;
            state_ = enumeration_state::addressed;
            action_ = enumeration_action::get_configuration;
            return true;
        case enumeration_event::configuration_complete:
            if (state_ != enumeration_state::addressed)
                return false;
            action_ = enumeration_action::set_configuration;
            return true;
        case enumeration_event::set_configuration_complete:
            if (state_ != enumeration_state::addressed)
                return false;
            state_ = enumeration_state::configured;
            action_ = enumeration_action::none;
            return true;
        case enumeration_event::suspend:
            if (state_ != enumeration_state::addressed &&
                state_ != enumeration_state::configured)
                return false;
            state_ = enumeration_state::suspended;
            action_ = enumeration_action::none;
            return true;
        case enumeration_event::resume:
            if (state_ != enumeration_state::suspended)
                return false;
            state_ = enumeration_state::configured;
            return true;
        case enumeration_event::detach:
            if (state_ == enumeration_state::detached)
                return false;
            state_ = enumeration_state::detached;
            action_ = enumeration_action::none;
            return true;
        case enumeration_event::failure:
            if (state_ == enumeration_state::detached)
                return false;
            state_ = enumeration_state::error;
            action_ = enumeration_action::none;
            return true;
        case enumeration_event::unsupported:
            if (state_ == enumeration_state::detached)
                return false;
            state_ = enumeration_state::unsupported;
            action_ = enumeration_action::none;
            return true;
        }
        return false;
    }
};

enum class transfer_state : uint8_t { free, pending, cancel_requested, canceled, completed, failed };

struct transfer_handle {
    uint16_t slot = UINT16_MAX;
    uint16_t generation = 0;

    bool valid() const { return slot != UINT16_MAX; }
};

struct transfer_request {
    transfer_state state = transfer_state::free;
    void* buffer = nullptr;
    uint32_t length = 0;
    int64_t result = 0;
    uint16_t generation = 0;
};

template <unsigned Capacity>
class transfer_pool {
    transfer_request requests_[Capacity]{};

    bool owns(transfer_handle handle) const {
        return handle.valid() && handle.slot < Capacity &&
               requests_[handle.slot].generation == handle.generation &&
               requests_[handle.slot].state != transfer_state::free;
    }

    public:
    bool submit(void* buffer, uint32_t length, transfer_handle& handle) {
        handle = {};
        if (buffer == nullptr || length == 0)
            return false;
        for (unsigned i = 0; i < Capacity; ++i) {
            transfer_request& request = requests_[i];
            if (request.state != transfer_state::free)
                continue;
            ++request.generation;
            if (request.generation == 0)
                ++request.generation;
            request.state = transfer_state::pending;
            request.buffer = buffer;
            request.length = length;
            request.result = 0;
            handle = {static_cast<uint16_t>(i), request.generation};
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

    bool complete(transfer_handle handle, int64_t result) {
        if (!owns(handle))
            return false;
        transfer_request& request = requests_[handle.slot];
        if (request.state != transfer_state::pending &&
            request.state != transfer_state::cancel_requested)
            return false;
        if (request.state == transfer_state::cancel_requested) {
            request.state = transfer_state::canceled;
            request.result = -125;
        } else {
            request.state = result < 0 ? transfer_state::failed : transfer_state::completed;
            request.result = result;
        }
        return true;
    }

    bool release(transfer_handle handle) {
        if (!owns(handle))
            return false;
        transfer_request& request = requests_[handle.slot];
        if (request.state != transfer_state::canceled &&
            request.state != transfer_state::completed &&
            request.state != transfer_state::failed)
            return false;
        request.state = transfer_state::free;
        request.buffer = nullptr;
        request.length = 0;
        request.result = 0;
        return true;
    }

    transfer_state state(transfer_handle handle) const {
        return owns(handle) ? requests_[handle.slot].state : transfer_state::free;
    }

    int64_t result(transfer_handle handle) const {
        return owns(handle) ? requests_[handle.slot].result : -22;
    }
};

struct driver_binding;
using driver_match = bool (*)(const device_model&, const interface_model&);
using driver_bind = bool (*)(const device_model&, const interface_model&, void**);
using driver_unbind = void (*)(void*);

struct driver_entry {
    const char* name = nullptr;
    driver_match match = nullptr;
    driver_bind bind = nullptr;
    driver_unbind unbind = nullptr;
};

struct driver_binding {
    const driver_entry* driver = nullptr;
    void* context = nullptr;
};

class driver_registry {
    driver_entry entries_[max_drivers]{};
    bool used_[max_drivers]{};

    public:
    bool register_driver(const driver_entry& entry) {
        if (entry.name == nullptr || entry.match == nullptr || entry.bind == nullptr)
            return false;
        for (unsigned i = 0; i < max_drivers; ++i)
            if (used_[i] && entries_[i].match == entry.match)
                return false;
        for (unsigned i = 0; i < max_drivers; ++i) {
            if (!used_[i]) {
                entries_[i] = entry;
                used_[i] = true;
                return true;
            }
        }
        return false;
    }

    bool unregister_driver(driver_match match) {
        if (match == nullptr)
            return false;
        for (unsigned i = 0; i < max_drivers; ++i) {
            if (used_[i] && entries_[i].match == match) {
                used_[i] = false;
                entries_[i] = {};
                return true;
            }
        }
        return false;
    }

    bool bind(const device_model& device, const interface_model& interface,
              driver_binding& result) const {
        result = {};
        for (unsigned i = 0; i < max_drivers; ++i) {
            if (!used_[i] || !entries_[i].match(device, interface))
                continue;
            void* context = nullptr;
            if (!entries_[i].bind(device, interface, &context))
                return false;
            result = {&entries_[i], context};
            return true;
        }
        return false;
    }
};

}
