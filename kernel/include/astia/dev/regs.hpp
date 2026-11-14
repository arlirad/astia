#pragma once

#include <astia/arch/io.hpp>
#include <astia/common.hpp>
#include <astia/dev/capabilities.hpp>

namespace astia::dev {

    /// C++ device-driver view of a mapped MMIO register block.  The wrapper
    /// owns no mapping and is not part of the C module ABI; the caller owns
    /// the mapped address and must keep it valid for the wrapper's lifetime.
    template <typename T>
    class regs {
        static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4,
                      "register width must be 1, 2 or 4 bytes");

        public:
        constexpr regs() = default;
        explicit regs(volatile void* base) : _base(static_cast<volatile u8*>(base)) {}

        volatile T& operator[](usize offset) const {
            return *reinterpret_cast<volatile T*>(_base + offset);
        }

        volatile void* base() const {
            return _base;
        }

        private:
        volatile u8* _base = nullptr;
    };

    /// C++ device-driver view of a port-space register block.  Port I/O is
    /// architecture-owned and intentionally separated from MMIO: a port
    /// number is not a dereferenceable address.  The default backend is an
    /// architecture adapter; host tests and other architectures can supply
    /// a backend with the same readN/writeN static operations.
    template <typename T, typename Backend = arch::pio>
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
                if constexpr (sizeof(T) == 1)
                    return Backend::read8(_port);
                else if constexpr (sizeof(T) == 2)
                    return Backend::read16(_port);
                else
                    return Backend::read32(_port);
            }

            void operator=(T value) const {
                if constexpr (sizeof(T) == 1)
                    Backend::write8(_port, value);
                else if constexpr (sizeof(T) == 2)
                    Backend::write16(_port, value);
                else
                    Backend::write32(_port, value);
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
