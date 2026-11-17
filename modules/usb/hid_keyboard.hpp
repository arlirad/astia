#pragma once

#include "usb_protocol.hpp"

#include <stddef.h>

namespace astia::usb::hid {

constexpr uint8_t class_hid = 3;
constexpr uint8_t subclass_boot = 1;
constexpr uint8_t protocol_keyboard = 1;
constexpr uint8_t usage_page_keyboard = 7;
constexpr uint8_t usage_error_rollover = 1;
constexpr uint8_t usage_error_post_fail = 2;
constexpr uint8_t usage_error_undefined = 3;
constexpr uint8_t usage_left_control = 0xe0;
constexpr uint8_t usage_right_gui = 0xe7;

enum class report_status : uint8_t { accepted, malformed, rollover, error };

struct boot_report {
    uint8_t modifiers = 0;
    uint8_t reserved = 0;
    uint8_t keys[6]{};
};

inline report_status decode_boot_report(const uint8_t* data, uint32_t length,
                                        boot_report& result) {
    result = {};
    if (data == nullptr || length < 8)
        return report_status::malformed;
    result.modifiers = data[0];
    result.reserved = data[1];
    bool rollover = false;
    bool error = false;
    for (unsigned i = 0; i < 6; ++i) {
        result.keys[i] = data[2 + i];
        if (result.keys[i] == usage_error_rollover)
            rollover = true;
        if (result.keys[i] == usage_error_post_fail || result.keys[i] == usage_error_undefined)
            error = true;
    }
    if (rollover)
        return report_status::rollover;
    if (error)
        return report_status::error;
    return report_status::accepted;
}

struct input_sink {
    void* context = nullptr;
    bool (*put)(void*, uint8_t) = nullptr;

    bool write(uint8_t value) const {
        return put != nullptr && put(context, value);
    }
};

class keymap {
    static bool alpha(uint8_t usage) { return usage >= 0x04 && usage <= 0x1d; }

    static uint8_t alpha_byte(uint8_t usage, uint8_t modifiers, bool caps) {
        const bool shift = (modifiers & ((1u << 1) | (1u << 5))) != 0;
        const bool upper = shift != caps;
        return static_cast<uint8_t>((upper ? 'A' : 'a') + usage - 0x04);
    }

    static bool shifted_digit(uint8_t usage, uint8_t& value) {
        static constexpr char shifted[] = "!@#$%^&*()";
        if (usage < 0x1e || usage > 0x27)
            return false;
        const unsigned index = usage - 0x1e;
        value = shifted[index];
        return true;
    }

    static bool plain_digit(uint8_t usage, uint8_t& value) {
        static constexpr char digits[] = "1234567890";
        if (usage < 0x1e || usage > 0x27)
            return false;
        value = digits[usage - 0x1e];
        return true;
    }

    static bool control(uint8_t usage, uint8_t modifiers, uint8_t& value) {
        if ((modifiers & ((1u << 0) | (1u << 4))) == 0)
            return false;
        if (alpha(usage)) {
            value = static_cast<uint8_t>(usage - 0x04 + 1);
            return true;
        }
        if (usage == 0x2c) {
            value = 0;
            return true;
        }
        if (usage == 0x2f) {
            value = 0x1b;
            return true;
        }
        if (usage == 0x30) {
            value = 0x1c;
            return true;
        }
        if (usage == 0x31) {
            value = 0x1d;
            return true;
        }
        if (usage == 0x32) {
            value = 0x1e;
            return true;
        }
        return false;
    }

    static bool sequence(uint8_t usage, const uint8_t*& data, uint8_t& length) {
        static constexpr uint8_t up[] = {0x1b, '[', 'A'};
        static constexpr uint8_t down[] = {0x1b, '[', 'B'};
        static constexpr uint8_t right[] = {0x1b, '[', 'C'};
        static constexpr uint8_t left[] = {0x1b, '[', 'D'};
        static constexpr uint8_t home[] = {0x1b, '[', 'H'};
        static constexpr uint8_t end[] = {0x1b, '[', 'F'};
        static constexpr uint8_t insert[] = {0x1b, '[', '2', '~'};
        static constexpr uint8_t erase[] = {0x1b, '[', '3', '~'};
        static constexpr uint8_t page_up[] = {0x1b, '[', '5', '~'};
        static constexpr uint8_t page_down[] = {0x1b, '[', '6', '~'};
        static constexpr uint8_t f1[] = {0x1b, 'O', 'P'};
        static constexpr uint8_t f2[] = {0x1b, 'O', 'Q'};
        static constexpr uint8_t f3[] = {0x1b, 'O', 'R'};
        static constexpr uint8_t f4[] = {0x1b, 'O', 'S'};
        static constexpr uint8_t f5[] = {0x1b, '[', '1', '5', '~'};
        static constexpr uint8_t f6[] = {0x1b, '[', '1', '7', '~'};
        static constexpr uint8_t f7[] = {0x1b, '[', '1', '8', '~'};
        static constexpr uint8_t f8[] = {0x1b, '[', '1', '9', '~'};
        static constexpr uint8_t f9[] = {0x1b, '[', '2', '0', '~'};
        static constexpr uint8_t f10[] = {0x1b, '[', '2', '1', '~'};
        static constexpr uint8_t f11[] = {0x1b, '[', '2', '3', '~'};
        static constexpr uint8_t f12[] = {0x1b, '[', '2', '4', '~'};
        switch (usage) {
        case 0x4f: data = right; length = sizeof right; return true;
        case 0x50: data = left; length = sizeof left; return true;
        case 0x51: data = down; length = sizeof down; return true;
        case 0x52: data = up; length = sizeof up; return true;
        case 0x4a: data = home; length = sizeof home; return true;
        case 0x4d: data = end; length = sizeof end; return true;
        case 0x49: data = insert; length = sizeof insert; return true;
        case 0x4c: data = erase; length = sizeof erase; return true;
        case 0x4b: data = page_up; length = sizeof page_up; return true;
        case 0x4e: data = page_down; length = sizeof page_down; return true;
        case 0x3a: data = f1; length = sizeof f1; return true;
        case 0x3b: data = f2; length = sizeof f2; return true;
        case 0x3c: data = f3; length = sizeof f3; return true;
        case 0x3d: data = f4; length = sizeof f4; return true;
        case 0x3e: data = f5; length = sizeof f5; return true;
        case 0x3f: data = f6; length = sizeof f6; return true;
        case 0x40: data = f7; length = sizeof f7; return true;
        case 0x41: data = f8; length = sizeof f8; return true;
        case 0x42: data = f9; length = sizeof f9; return true;
        case 0x43: data = f10; length = sizeof f10; return true;
        case 0x44: data = f11; length = sizeof f11; return true;
        case 0x45: data = f12; length = sizeof f12; return true;
        default: return false;
        }
    }

    public:
    bool emit(uint8_t usage, uint8_t modifiers, input_sink sink, bool caps = false) const {
        if (usage == 0 || usage <= 3)
            return false;
        if (usage >= usage_left_control && usage <= usage_right_gui)
            return true;
        if (usage == 0x39)
            return true;
        if (sink.put == nullptr)
            return false;
        uint8_t value = 0;
        if (control(usage, modifiers, value))
            return sink.write(value);
        if (alpha(usage))
            return sink.write(alpha_byte(usage, modifiers, caps));
        if (plain_digit(usage, value)) {
            if ((modifiers & ((1u << 1) | (1u << 5))) != 0)
                return shifted_digit(usage, value) && sink.write(value);
            return sink.write(value);
        }
        switch (usage) {
        case 0x28: return sink.write('\r');
        case 0x29: return sink.write(0x1b);
        case 0x2a: return sink.write(0x7f);
        case 0x2b: return sink.write('\t');
        case 0x2c: return sink.write(' ');
        case 0x2d: value = (modifiers & ((1u << 1) | (1u << 5))) ? '_' : '-'; return sink.write(value);
        case 0x2e: value = (modifiers & ((1u << 1) | (1u << 5))) ? '+' : '='; return sink.write(value);
        case 0x2f: value = (modifiers & ((1u << 1) | (1u << 5))) ? '{' : '['; return sink.write(value);
        case 0x30: value = (modifiers & ((1u << 1) | (1u << 5))) ? '}' : ']'; return sink.write(value);
        case 0x31: value = (modifiers & ((1u << 1) | (1u << 5))) ? '|' : '\\'; return sink.write(value);
        case 0x33: value = (modifiers & ((1u << 1) | (1u << 5))) ? ':' : ';'; return sink.write(value);
        case 0x34: value = (modifiers & ((1u << 1) | (1u << 5))) ? '"' : '\''; return sink.write(value);
        case 0x35: value = (modifiers & ((1u << 1) | (1u << 5))) ? '~' : '`'; return sink.write(value);
        case 0x36: value = (modifiers & ((1u << 1) | (1u << 5))) ? '<' : ','; return sink.write(value);
        case 0x37: value = (modifiers & ((1u << 1) | (1u << 5))) ? '>' : '.'; return sink.write(value);
        case 0x38: value = (modifiers & ((1u << 1) | (1u << 5))) ? '?' : '/'; return sink.write(value);
        default: break;
        }
        const uint8_t* bytes = nullptr;
        uint8_t count = 0;
        if (sequence(usage, bytes, count)) {
            bool accepted = true;
            if ((modifiers & ((1u << 2) | (1u << 6))) != 0)
                accepted = sink.write(0x1b);
            for (uint8_t i = 0; accepted && i < count; ++i)
                accepted = sink.write(bytes[i]);
            return accepted;
        }
        return false;
    }
};

class boot_keyboard {
    bool previous_[256]{};
    uint8_t modifiers_ = 0;
    bool caps_lock_ = false;
    bool synchronized_ = true;
    keymap map_{};

    static bool contains(const bool* keys, uint8_t usage) { return keys[usage]; }

    public:
    report_status receive(const uint8_t* data, uint32_t length, input_sink sink) {
        boot_report report{};
        const report_status status = decode_boot_report(data, length, report);
        if (status != report_status::accepted) {
            synchronized_ = false;
            return status;
        }
        bool current[256]{};
        for (uint8_t usage : report.keys)
            if (usage >= 4)
                current[usage] = true;
        bool accepted = true;
        if (synchronized_) {
            for (unsigned usage = 4; usage < 256; ++usage)
                if (previous_[usage] && !contains(current, static_cast<uint8_t>(usage)))
                    previous_[usage] = false;
        } else {
            for (unsigned usage = 4; usage < 256; ++usage)
                previous_[usage] = false;
        }
        for (unsigned usage = 4; usage < 256; ++usage) {
            if (current[usage] && !previous_[usage]) {
                if (usage == 0x39)
                    caps_lock_ = !caps_lock_;
                accepted = accepted && map_.emit(static_cast<uint8_t>(usage), report.modifiers,
                                                 sink, caps_lock_);
            }
            previous_[usage] = current[usage];
        }
        modifiers_ = report.modifiers;
        synchronized_ = true;
        return accepted ? report_status::accepted : report_status::error;
    }

    uint8_t modifiers() const { return modifiers_; }
    bool caps_lock() const { return caps_lock_; }
    bool synchronized() const { return synchronized_; }
};

inline bool is_boot_keyboard(const interface_model& interface) {
    return interface.interface_class == class_hid && interface.interface_subclass == subclass_boot &&
           interface.interface_protocol == protocol_keyboard;
}

inline const endpoint_model* interrupt_in_endpoint(const interface_model& interface) {
    for (unsigned i = 0; i < interface.endpoint_count; ++i) {
        const endpoint_model& endpoint = interface.endpoints[i];
        if (endpoint.in && endpoint.type == transfer_type::interrupt)
            return &endpoint;
    }
    return nullptr;
}

enum class device_state : uint8_t { detached, discovered, protocol_pending, running, stopping, failed };

struct transport {
    void* context = nullptr;
    bool (*control)(void*, const setup_packet&, void*, uint32_t, transfer_handle&) = nullptr;
    bool (*interrupt_in)(void*, uint8_t, void*, uint32_t, uint8_t, transfer_handle&) = nullptr;
    bool (*cancel)(void*, transfer_handle) = nullptr;
};

class keyboard_device {
    const device_model* device_ = nullptr;
    const interface_model* interface_ = nullptr;
    const endpoint_model* endpoint_ = nullptr;
    transport transport_{};
    input_sink sink_{};
    boot_keyboard decoder_{};
    uint8_t report_[8]{};
    uint8_t interface_number_ = 0;
    uint8_t slot_ = 0;
    transfer_handle control_handle_{};
    transfer_handle interrupt_handle_{};
    device_state state_ = device_state::detached;

    bool submit_interrupt() {
        if (state_ != device_state::running || transport_.interrupt_in == nullptr ||
            endpoint_ == nullptr)
            return false;
        return transport_.interrupt_in(transport_.context, endpoint_->address, report_, sizeof report_,
                                       endpoint_->interval, interrupt_handle_);
    }

    public:
    bool discover(const device_model& device, const interface_model& interface, uint8_t slot,
                  transport transport, input_sink sink) {
        const endpoint_model* interrupt = interrupt_in_endpoint(interface);
        if (!is_boot_keyboard(interface) || !interface.hid.present || interrupt == nullptr ||
            interrupt->max_packet_size < 8 || interrupt->interval == 0 ||
            transport.control == nullptr || transport.interrupt_in == nullptr)
            return false;
        device_ = &device;
        interface_ = &interface;
        endpoint_ = interrupt_in_endpoint(interface);
        interface_number_ = interface.number;
        slot_ = slot;
        transport_ = transport;
        sink_ = sink;
        state_ = device_state::discovered;
        return true;
    }

    bool start() {
        if (state_ != device_state::discovered || transport_.control == nullptr)
            return false;
        const setup_packet set_protocol{0x21, 0x0b, 0, interface_number_, 0};
        if (!transport_.control(transport_.context, set_protocol, nullptr, 0, control_handle_)) {
            state_ = device_state::failed;
            return false;
        }
        state_ = device_state::protocol_pending;
        return true;
    }

    bool control_complete(transfer_handle handle, int64_t result) {
        if (state_ != device_state::protocol_pending || handle.slot != control_handle_.slot ||
            handle.generation != control_handle_.generation || result < 0)
            return false;
        control_handle_ = {};
        state_ = device_state::running;
        if (!submit_interrupt()) {
            state_ = device_state::failed;
            return false;
        }
        return true;
    }

    bool interrupt_complete(transfer_handle handle, int64_t result, uint32_t length) {
        if (state_ != device_state::running || handle.slot != interrupt_handle_.slot ||
            handle.generation != interrupt_handle_.generation)
            return false;
        interrupt_handle_ = {};
        if (result < 0) {
            state_ = device_state::failed;
            return false;
        }
        const uint32_t actual = length > sizeof report_ ? sizeof report_ : length;
        const report_status status = decoder_.receive(report_, actual, sink_);
        if (status == report_status::malformed || status == report_status::error) {
            state_ = device_state::failed;
            return false;
        }
        if (!submit_interrupt()) {
            state_ = device_state::failed;
            return false;
        }
        return true;
    }

    bool stop() {
        if (state_ == device_state::detached || state_ == device_state::stopping)
            return false;
        state_ = device_state::stopping;
        bool canceled = true;
        if (control_handle_.valid() && transport_.cancel != nullptr)
            canceled = transport_.cancel(transport_.context, control_handle_) && canceled;
        if (interrupt_handle_.valid() && transport_.cancel != nullptr)
            canceled = transport_.cancel(transport_.context, interrupt_handle_) && canceled;
        state_ = device_state::detached;
        device_ = nullptr;
        interface_ = nullptr;
        endpoint_ = nullptr;
        transport_ = {};
        control_handle_ = {};
        interrupt_handle_ = {};
        return canceled;
    }

    bool reset() {
        const bool was_active = state_ == device_state::running ||
                                state_ == device_state::protocol_pending;
        if (was_active)
            stop();
        return !was_active || state_ == device_state::detached;
    }

    device_state state() const { return state_; }
    uint8_t slot() const { return slot_; }
    uint8_t interface_number() const { return interface_number_; }
    const endpoint_model* endpoint() const { return endpoint_; }
    transfer_handle control_handle() const { return control_handle_; }
    transfer_handle interrupt_handle() const { return interrupt_handle_; }
};

} // namespace astia::usb::hid
