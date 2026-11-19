#pragma once

#include <astia/arch/interrupts.hpp>
#include <astia/common.hpp>
#include <astia/hw/pci.hpp>
#include <astia/io.hpp>
#include <astia/mm/dma_buffer.hpp>
#include <astia/mm/iovec.hpp>
#include <astia/mm/shptr.hpp>
#include <astia/net.hpp>

namespace astia::module {

    using net_device     = net::netdev;
    using netdev         = net::netdev;
    using net_tx_request = net::tx_request;

    class msix_route final {
        public:
        msix_route() = default;
        ~msix_route();
        msix_route(const msix_route&)            = delete;
        msix_route& operator=(const msix_route&) = delete;
        msix_route(msix_route&& other) noexcept;
        msix_route& operator=(msix_route&& other) noexcept;

        bool request(hw::pci_device& device, arch::interrupt_vector_handler handler,
                     void* context);
        bool program();
        bool enable();
        bool unmask();
        bool disable();
        bool release();
        bool active() const {
            return _vector.valid();
        }
        u32 table_index() const {
            return _table_index;
        }
        bool registered() const {
            return _registered;
        }

        public:
        hw::pci_device*        _device = nullptr;
        arch::interrupt_vector _vector{};
        u32                    _table_offset    = 0;
        u32                    _table_index     = 0;
        u8                     _table_bar       = 0;
        bool                   _mapped_by_route = false;
        bool                   _registered      = false;
        bool                   _vector_masked   = false;
        bool                   _vector_unmasked = false;
        bool                   _disable_pending = false;
        bool                   _enabled         = false;
    };

    bool        pci_enable_bus_master(hw::pci_device& device);
    inline bool pci_enable_bus_master(hw::pci_device* device) {
        return device != nullptr && module::pci_enable_bus_master(*device);
    }
    bool        pci_disable_bus_master(hw::pci_device& device);
    inline bool pci_disable_bus_master(hw::pci_device* device) {
        return device != nullptr && module::pci_disable_bus_master(*device);
    }
    bool        pci_disable_msix(hw::pci_device& device);
    inline bool pci_disable_msix(hw::pci_device* device) {
        return device != nullptr && pci_disable_msix(*device);
    }
    bool        pci_map_bar(hw::pci_device& device, u8 index);
    inline bool pci_map_bar(hw::pci_device* device, u8 index) {
        return device != nullptr && module::pci_map_bar(*device, index);
    }
    bool        pci_unmap_bar(hw::pci_device& device, u8 index);
    inline bool pci_unmap_bar(hw::pci_device* device, u8 index) {
        return device != nullptr && module::pci_unmap_bar(*device, index);
    }
    u64        pci_bar_mapped(const hw::pci_device& device, u8 index);
    inline u64 pci_bar_mapped(const hw::pci_device* device, u8 index) {
        return device == nullptr ? 0 : pci_bar_mapped(*device, index);
    }
    u64        pci_bar_size(const hw::pci_device& device, u8 index);
    inline u64 pci_bar_size(const hw::pci_device* device, u8 index) {
        return device == nullptr ? 0 : pci_bar_size(*device, index);
    }
    u32        pci_read32(const hw::pci_device& device, u16 offset);
    inline u32 pci_read32(const hw::pci_device* device, u16 offset) {
        return device == nullptr ? ~u32{0} : module::pci_read32(*device, offset);
    }
    u16        pci_read16(const hw::pci_device& device, u16 offset);
    inline u16 pci_read16(const hw::pci_device* device, u16 offset) {
        return device == nullptr ? ~u16{0} : module::pci_read16(*device, offset);
    }
    bool        pci_write16(const hw::pci_device& device, u16 offset, u16 value);
    inline bool pci_write16(const hw::pci_device* device, u16 offset, u16 value) {
        return device != nullptr && module::pci_write16(*device, offset, value);
    }
    bool pci_msix_request(hw::pci_device* device, arch::interrupt_vector_handler handler,
                          void* context, msix_route* route);
    bool pci_msix_program(msix_route* route);
    bool pci_msix_enable(msix_route* route);
    bool pci_msix_unmask(msix_route* route);
    bool pci_msix_disable(msix_route* route);
    bool pci_msix_release(msix_route* route);

    class user_pin final {
        public:
        user_pin() = default;
        ~user_pin();
        user_pin(const user_pin&)            = delete;
        user_pin& operator=(const user_pin&) = delete;
        user_pin(user_pin&& other) noexcept;
        user_pin& operator=(user_pin&& other) noexcept;

        bool acquire(void* address, u32 length, io::direction direction);
        void release();
        bool valid() const {
            return static_cast<bool>(_value);
        }
        u64 physical() const {
            return _value ? _value->physical() : 0;
        }

        private:
        mm::shptr<mm::iovec> _value;
    };

    bool copy_out(void* address, u32 length, const void* source);
    bool copy_in(void* destination, u32 length, const void* address);
    bool pin_user_write(void* address, u32 length, u64* physical, user_pin** token);
    bool pin_user_read(const void* address, u32 length, u64* physical, user_pin** token);
    void unpin(user_pin* token);
    int  read_registered_async(const char* path, void* buffer, u32 length, u64 offset);
    int  write_registered_async(const char* path, const void* buffer, u32 length,
                                u64 offset);

    void log(const char* source, const char* message);
    void log_u64(const char* source, const char* label, u64 value);
    int  callback_context();

} // namespace astia::module
