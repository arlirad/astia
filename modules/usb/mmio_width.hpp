#pragma once

#include <astia/core/endian.hpp>
#include <astia/dev/regs.hpp>

#include <stdint.h>

namespace astia::usb::xhci {
    struct system_mmio {
        static uint32_t read32(volatile void* base, unsigned offset) {
            const astia::dev::regs<uint32_t> words{base};

            return astia::core::le<uint32_t>(words[offset]).get();
        }

        static void write32(volatile void* base, unsigned offset, uint32_t value) {
            const astia::dev::regs<uint32_t> words{base};

            words[offset] = astia::core::le<uint32_t>(value).get();
        }
    };

    template <typename Backend>
    class mmio_access {
        public:
        constexpr mmio_access() = default;

        explicit mmio_access(volatile void* base) : base_(base) {}

        template <typename T>
        T read(unsigned offset) const {
            static_assert(sizeof(T) == 4 || sizeof(T) == 8,
                          "xHCI access is one aligned 32-bit access or a 32-bit pair");

            if constexpr (sizeof(T) == 4) {
                return static_cast<T>(Backend::read32(base_, offset));
            }
            else {
                const uint64_t low  = Backend::read32(base_, offset);
                const uint64_t high = Backend::read32(base_, offset + 4);

                return static_cast<T>(low | (high << 32));
            }
        }

        template <typename T>
        void write(unsigned offset, T value) const {
            static_assert(sizeof(T) == 4 || sizeof(T) == 8,
                          "xHCI access is one aligned 32-bit access or a 32-bit pair");

            if constexpr (sizeof(T) == 4) {
                Backend::write32(base_, offset, static_cast<uint32_t>(value));
            }
            else {
                const uint64_t wide = static_cast<uint64_t>(value);

                Backend::write32(base_, offset, static_cast<uint32_t>(wide));
                Backend::write32(base_, offset + 4, static_cast<uint32_t>(wide >> 32));
            }
        }

        bool mapped() const {
            return base_ != nullptr;
        }

        private:
        volatile void* base_ = nullptr;
    };

    using mmio = mmio_access<system_mmio>;
}
