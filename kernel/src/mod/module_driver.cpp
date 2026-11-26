#include "module_driver.hpp"

#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/hw/pci.hpp>
#include <astia/log.hpp>
#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/iovec.hpp>
#include <astia/mm/shptr.hpp>
#include <astia/net.hpp>

#include "../dev/chr/tty/virtual_tty.hpp"
#include "../sys/aio.hpp"
#include "../sys/user_mem.hpp"

namespace astia::module {
    namespace {
        volatile u32* table_entry(msix_route& route) {
            if (route._device == nullptr || !route._vector.valid())
                return nullptr;

            hw::pci_device&               device    = *route._device;
            const hw::pci_interrupt_info& interrupt = device.interrupt;

            if (!interrupt.msix || route._table_bar != interrupt.table_bar ||
                route._table_offset != interrupt.table_offset ||
                route._table_index >= interrupt.vector_count ||
                route._table_index >= 2048 || route._table_bar >= device.bar_count)
                return nullptr;

            hw::pci_bar& bar = device.bars[route._table_bar];

            if (!bar.present || route._table_offset > bar.size ||
                16 > bar.size - route._table_offset ||
                route._table_index > (bar.size - route._table_offset - 16) / 16)
                return nullptr;

            const bool mapped_before = bar.mapped_valid;

            if (!mapped_before && !hw::pci_map_bar(device, route._table_bar))
                return nullptr;

            route._mapped_by_route = route._mapped_by_route || !mapped_before;

            const u64 address =
                bar.mapped + route._table_offset + route._table_index * 16;

            return reinterpret_cast<volatile u32*>(address);
        }

        void mask_table(volatile u32* entry, bool masked) {
            if (entry == nullptr)
                return;

            u32 control = entry[3];

            if (masked)
                control |= 1u;
            else
                control &= ~1u;

            entry[3] = control;

            asm volatile("" ::: "memory");
        }
    } // namespace

    msix_route::~msix_route() {
        release();
    }

    msix_route::msix_route(msix_route&& other) noexcept
        : _device(other._device), _vector(other._vector),
          _table_offset(other._table_offset), _table_index(other._table_index),
          _table_bar(other._table_bar), _mapped_by_route(other._mapped_by_route),
          _registered(other._registered), _vector_masked(other._vector_masked),
          _vector_unmasked(other._vector_unmasked),
          _disable_pending(other._disable_pending), _enabled(other._enabled) {
        other._device          = nullptr;
        other._vector          = {};
        other._table_offset    = 0;
        other._table_index     = 0;
        other._table_bar       = 0;
        other._mapped_by_route = false;
        other._registered      = false;
        other._vector_masked   = false;
        other._vector_unmasked = false;
        other._disable_pending = false;
        other._enabled         = false;
    }

    msix_route& msix_route::operator=(msix_route&& other) noexcept {
        if (this != &other) {
            release();

            _device                = other._device;
            _vector                = other._vector;
            _table_offset          = other._table_offset;
            _table_index           = other._table_index;
            _table_bar             = other._table_bar;
            _mapped_by_route       = other._mapped_by_route;
            _registered            = other._registered;
            _vector_masked         = other._vector_masked;
            _vector_unmasked       = other._vector_unmasked;
            _disable_pending       = other._disable_pending;
            _enabled               = other._enabled;
            other._device          = nullptr;
            other._vector          = {};
            other._table_offset    = 0;
            other._table_index     = 0;
            other._table_bar       = 0;
            other._mapped_by_route = false;
            other._registered      = false;
            other._vector_masked   = false;
            other._vector_unmasked = false;
            other._disable_pending = false;
            other._enabled         = false;
        }

        return *this;
    }

    bool msix_route::request(hw::pci_device&                device,
                             arch::interrupt_vector_handler handler, void* context) {
        if (_registered || _vector.valid() || handler == nullptr ||
            !device.interrupt.msix || device.interrupt.vector_count == 0 ||
            device.interrupt_blocker == hw::pci_interrupt_blocker::invalid_capability)
            return false;

        arch::interrupt_vector vector{};

        if (!arch::register_interrupt_vector(handler, context, vector))
            return false;

        _device          = &device;
        _vector          = vector;
        _table_offset    = device.interrupt.table_offset;
        _table_index     = 0;
        _table_bar       = device.interrupt.table_bar;
        _mapped_by_route = false;
        _registered      = true;
        _vector_masked   = false;
        _vector_unmasked = false;
        _disable_pending = false;
        _enabled         = false;

        arch::msi_message message{};

        if (!arch::interrupt_vector_message(_vector, message)) {
            release();

            return false;
        }

        return true;
    }

    bool msix_route::program() {
        if (!_registered || !_vector.valid() || !arch::mask_interrupt_vector(_vector))
            return false;

        _vector_masked = true;

        volatile u32*     entry = table_entry(*this);
        arch::msi_message message{};

        if (entry == nullptr || !arch::interrupt_vector_message(_vector, message))
            return false;

        mask_table(entry, true);

        entry[0] = message.address_low;
        entry[1] = message.address_high;
        entry[2] = message.data;

        asm volatile("" ::: "memory");

        return true;
    }

    bool msix_route::enable() {
        if (!_registered || !_vector.valid() || _device == nullptr)
            return false;

        volatile u32* entry = table_entry(*this);

        u16 control = 0;

        if (entry == nullptr ||
            !hw::pci_config_read16(_device->bdf,
                                   static_cast<u16>(_device->interrupt.capability + 2),
                                   control))
            return false;

        _disable_pending = true;

        if (!hw::pci_config_write16(
                _device->bdf, static_cast<u16>(_device->interrupt.capability + 2),
                static_cast<u16>((control | (1u << 15)) & ~(1u << 14))))
            return false;

        _enabled = true;

        return true;
    }

    bool msix_route::unmask() {
        if (!_registered || !_vector.valid() || !_enabled ||
            !arch::unmask_interrupt_vector(_vector))
            return false;

        _vector_unmasked = true;

        volatile u32* entry = table_entry(*this);
        if (entry == nullptr) {
            (void) disable();

            return false;
        }

        mask_table(entry, false);

        return true;
    }

    bool msix_route::disable() {
        bool result = true;

        if (_vector_unmasked) {
            if (!arch::mask_interrupt_vector(_vector)) {
                result = false;
            }
            else {
                _vector_unmasked = false;
                _vector_masked   = true;
            }

            volatile u32* entry = table_entry(*this);
            if (entry == nullptr)
                result = false;
            else
                mask_table(entry, true);
        }

        if (_disable_pending) {
            if (_device == nullptr) {
                result = false;
            }
            else {
                u16 control = 0;

                if (!hw::pci_config_read16(
                        _device->bdf, static_cast<u16>(_device->interrupt.capability + 2),
                        control) ||
                    !hw::pci_config_write16(
                        _device->bdf, static_cast<u16>(_device->interrupt.capability + 2),
                        static_cast<u16>(control & ~(1u << 15)))) {
                    result = false;
                }
                else {
                    _enabled         = false;
                    _disable_pending = false;
                }
            }
        }

        return result;
    }

    bool msix_route::release() {
        if (!_registered && !_vector.valid())
            return true;

        if (!disable())
            return false;

        if (_device != nullptr && _mapped_by_route) {
            if (!hw::pci_unmap_bar(*_device, _table_bar))
                return false;

            _mapped_by_route = false;
        }

        if (_registered && !arch::unregister_interrupt_vector(_vector))
            return false;

        _device          = nullptr;
        _vector          = {};
        _table_offset    = 0;
        _table_index     = 0;
        _table_bar       = 0;
        _mapped_by_route = false;
        _registered      = false;
        _vector_masked   = false;
        _vector_unmasked = false;
        _disable_pending = false;
        _enabled         = false;

        return true;
    }

    bool pci_enable_bus_master(hw::pci_device& device) {
        return hw::pci_enable_bus_master(device);
    }

    bool pci_disable_bus_master(hw::pci_device& device) {
        return hw::pci_disable_bus_master(device);
    }

    bool pci_disable_msix(hw::pci_device& device) {
        if (!device.interrupt.msix)
            return true;

        u16 control = 0;

        return hw::pci_config_read16(device.bdf,
                                     static_cast<u16>(device.interrupt.capability + 2),
                                     control) &&
               hw::pci_config_write16(
                   device.bdf, static_cast<u16>(device.interrupt.capability + 2),
                   static_cast<u16>((control | (1u << 14)) & ~(1u << 15)));
    }

    bool pci_restore_msix(hw::pci_device& device, u16 original_control) {
        if (!device.interrupt.msix)
            return true;

        u16 control = 0;

        if (!hw::pci_config_read16(
                device.bdf, static_cast<u16>(device.interrupt.capability + 2), control))
            return false;

        control = static_cast<u16>((control & ~0xc000u) | (original_control & 0xc000u));

        return hw::pci_config_write16(
            device.bdf, static_cast<u16>(device.interrupt.capability + 2), control);
    }

    bool pci_map_bar(hw::pci_device& device, u8 index) {
        return hw::pci_map_bar(device, index);
    }

    bool pci_unmap_bar(hw::pci_device& device, u8 index) {
        return hw::pci_unmap_bar(device, index);
    }

    u64 pci_bar_mapped(const hw::pci_device& device, u8 index) {
        return index < device.bar_count ? device.bars[index].mapped : 0;
    }

    u64 pci_bar_size(const hw::pci_device& device, u8 index) {
        return index < device.bar_count ? device.bars[index].size : 0;
    }

    u32 pci_read32(const hw::pci_device& device, u16 offset) {
        u32 value = ~u32{0};

        return hw::pci_config_read32(device.bdf, offset, value) ? value : ~u32{0};
    }

    u16 pci_read16(const hw::pci_device& device, u16 offset) {
        u16 value = ~u16{0};

        return hw::pci_config_read16(device.bdf, offset, value) ? value : ~u16{0};
    }

    bool pci_write16(const hw::pci_device& device, u16 offset, u16 value) {
        return hw::pci_config_write16(device.bdf, offset, value);
    }

    bool pci_msix_request(hw::pci_device* device, arch::interrupt_vector_handler handler,
                          void* context, msix_route* route) {
        return device != nullptr && route != nullptr &&
               route->request(*device, handler, context);
    }

    bool pci_msix_program(msix_route* route) {
        return route != nullptr && route->program();
    }

    bool pci_msix_enable(msix_route* route) {
        return route != nullptr && route->enable();
    }

    bool pci_msix_unmask(msix_route* route) {
        return route != nullptr && route->unmask();
    }

    bool pci_msix_disable(msix_route* route) {
        return route != nullptr && route->disable();
    }

    bool pci_msix_release(msix_route* route) {
        return route != nullptr && route->release();
    }

    user_pin::~user_pin() {
        release();
    }

    user_pin::user_pin(user_pin&& other) noexcept
        : _value(static_cast<mm::shptr<mm::iovec>&&>(other._value)) {}

    user_pin& user_pin::operator=(user_pin&& other) noexcept {
        if (this != &other) {
            release();

            _value = static_cast<mm::shptr<mm::iovec>&&>(other._value);
        }

        return *this;
    }

    bool user_pin::acquire(void* address, u32 length, io::direction direction) {
        release();

        if (address == nullptr || length == 0)
            return false;

        _value = mm::shptr<mm::iovec>(new mm::iovec());
        if (!_value)
            return false;

        if (sys::pin_user_span(span<u8>(static_cast<u8*>(address), length), direction,
                               *_value) < 0) {
            _value = {};

            return false;
        }

        return true;
    }

    void user_pin::release() {
        _value = {};
    }

    bool pin_user_write(void* address, u32 length, u64* physical, user_pin** token) {
        if (physical == nullptr || token == nullptr)
            return false;

        *physical = 0;
        *token    = new user_pin();

        if (*token == nullptr ||
            !(*token)->acquire(address, length, io::direction::input)) {
            delete *token;

            *token = nullptr;

            return false;
        }

        *physical = (*token)->physical();

        return true;
    }

    bool pin_user_read(const void* address, u32 length, u64* physical, user_pin** token) {
        if (!pin_user_write(const_cast<void*>(address), length, physical, token))
            return false;

        if ((*token)->acquire(const_cast<void*>(address), length, io::direction::output))
            return true;

        unpin(*token);

        *token    = nullptr;
        *physical = 0;

        return false;
    }

    void unpin(user_pin* token) {
        delete token;
    }

    bool copy_out(void* address, u32 length, const void* source) {
        return address != nullptr && source != nullptr && length != 0 &&
               sys::copy_out(span<u8>(static_cast<u8*>(address), length), source);
    }

    bool copy_in(void* destination, u32 length, const void* address) {
        return destination != nullptr && address != nullptr && length != 0 &&
               sys::copy_in(
                   destination,
                   span<u8>(const_cast<u8*>(static_cast<const u8*>(address)), length));
    }

    namespace {
        int registered_async(const char* path, void* buffer, u32 length, u64 offset,
                             u32 op, u32 flags) {
            if (path == nullptr || buffer == nullptr || length == 0)
                return -EINVAL;

            auto  file    = fs::open(path, flags);
            auto* current = proc::current_process();

            if (!file || current == nullptr)
                return -ENOENT;

            astia_rings* ring = nullptr;

            const long setup = sys::io_setup(8, 0, &ring);
            if (setup < 0)
                return static_cast<int>(setup);

            fd_t fd{-1};

            for (i32 value = 3; value < static_cast<i32>(proc::max_process_fds);
                 ++value) {
                if (current->set_descriptor(fd_t{value}, file)) {
                    fd = fd_t{value};

                    break;
                }
            }

            if (fd.value < 0) {
                (void) sys::io_destroy(ring);

                return -EMFILE;
            }

            astia_sqe* sqe = astia_sqe_slot(ring);
            if (sqe == nullptr) {
                current->clear_descriptor(fd);
                (void) sys::io_destroy(ring);

                return -EBUSY;
            }

            *sqe           = astia_sqe{};
            sqe->op        = op;
            sqe->fd        = fd.value;
            sqe->op_flags  = flags;
            sqe->offset    = offset;
            sqe->address   = reinterpret_cast<u64>(buffer);
            sqe->length    = length;
            sqe->user_data = 0xA51A;

            astia_sq_publish(ring, 1);

            const long submitted = sys::io_submit(ring, 1);
            if (submitted != 1) {
                current->clear_descriptor(fd);
                (void) sys::io_destroy(ring);

                return submitted < 0 ? static_cast<int>(submitted) : -EIO;
            }

            const bool was_enabled = arch::cpu::check_interrupts();
            if (!was_enabled)
                arch::cpu::interrupts();

            const u64 hz       = arch::timer_hz();
            const u64 deadline = hz == 0 ? 0 : arch::timer_ticks() + hz * 2;
            int       result   = -ETIMEDOUT;

            while (true) {
                astia_cqe* cqe = astia_cq_peek(ring);
                if (cqe != nullptr) {
                    result = cqe->user_data == 0xA51A ? cqe->result : -EIO;

                    astia_cq_consume(ring, 1);
                    break;
                }

                if (deadline != 0 && arch::timer_ticks() >= deadline)
                    break;

                arch::cpu::halt();
            }

            if (!was_enabled)
                arch::cpu::nointerrupts();

            current->clear_descriptor(fd);
            (void) sys::io_destroy(ring);

            return result == static_cast<int>(length) ? 0 : result;
        }
    } // namespace

    int read_registered_async(const char* path, void* buffer, u32 length, u64 offset) {
        return registered_async(path, buffer, length, offset, OP_READ,
                                ASTIA_AIO_O_RDONLY);
    }

    int write_registered_async(const char* path, const void* buffer, u32 length,
                               u64 offset) {
        return registered_async(path, const_cast<void*>(buffer), length, offset, OP_WRITE,
                                ASTIA_AIO_O_WRONLY);
    }

    void log(const char* source, const char* message) {
        astia::log::info("%s: %s", source == nullptr ? "module" : source,
                         message == nullptr ? "" : message);
    }

    void log_u64(const char* source, const char* label, u64 value) {
        astia::log::info("%s: %s=%llu", source == nullptr ? "module" : source,
                         label == nullptr ? "value" : label,
                         static_cast<unsigned long long>(value));
    }

    int callback_context() {
        return arch::cpu::in_interrupt() ? 1 : 0;
    }
} // namespace astia::module

namespace astia::mod {
    bool run_selftests() {
        const bool interrupts = arch::cpu::check_interrupts();
        if (!interrupts)
            arch::cpu::interrupts();

        const bool passed = hw::drivers().selftests();

        if (!interrupts)
            arch::cpu::nointerrupts();

        return passed;
    }
} // namespace astia::mod
