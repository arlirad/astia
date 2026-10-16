#pragma once

#include "io.hpp"
#include <astia/common.hpp>

// Typed access to a mapped register block, shaped after aretev's
// regs<T>: one mapped base, one element type, byte offsets.  The
// reference returned by operator[] is volatile so each access remains
// an actual device access:
//
//     regs<u32> regs { mapped_mmio };
//     regs[reg_status] = value;
//     u32 state = regs[reg_status];
//
// x86 port-space access is a different address space, so it has the
// separate ports<T> wrapper below.  A PIO UART must use ports<u8>, not
// pretend that its port numbers are MMIO addresses.

namespace astia::dev {

    template <typename T>
    class regs {
        static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4,
                      "register width must be 1, 2 or 4 bytes");

        public:
        constexpr regs() = default;
        explicit regs(volatile void* base) : _base(static_cast<volatile u8*>(base)) {}

        volatile T& operator[](u32 offset) const {
            return *reinterpret_cast<volatile T*>(_base + offset);
        }

        volatile void* base() const {
            return _base;
        }

        private:
        volatile u8* _base = nullptr;
    };

    /// Typed access to an x86 port-space register block.  This is kept
    /// distinct from regs<T>: port number 0x3f8 is not a dereferenceable
    /// virtual address.
    template <typename T>
    class ports {
        static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4,
                      "port width must be 1, 2 or 4 bytes");

        public:
        constexpr ports() = default;
        explicit constexpr ports(u16 base) : _base(base) {}

        class ref {
            public:
            constexpr explicit ref(u16 port) : _port(port) {}

            operator T() const {
                if constexpr (sizeof(T) == 1) {
                    return arch::x86_64::inb(_port);
                }
                else if constexpr (sizeof(T) == 2) {
                    return arch::x86_64::inw(_port);
                }
                else {
                    return arch::x86_64::inl(_port);
                }
            }

            void operator=(T value) const {
                if constexpr (sizeof(T) == 1) {
                    arch::x86_64::outb(_port, value);
                }
                else if constexpr (sizeof(T) == 2) {
                    arch::x86_64::outw(_port, value);
                }
                else {
                    arch::x86_64::outl(_port, value);
                }
            }

            private:
            u16 _port;
        };

        constexpr ref operator[](u32 offset) const {
            return ref{static_cast<u16>(_base + offset)};
        }

        constexpr u16 base() const {
            return _base;
        }

        private:
        u16 _base = 0;
    };

} // namespace astia::dev
