#pragma once

#include <astia/arch/io.hpp>
#include <astia/common.hpp>
#include <astia/dev/capabilities.hpp>

// Port I/O and MSR primitives shared by the x86_64 device drivers
// (pic, pit, lapic). Header-only: each is one instruction.

namespace astia::arch::x86_64 {
    inline void outb(u16 port, u8 value) {
        asm volatile("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
    }

    inline u8 inb(u16 port) {
        u8 value = 0;

        asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");

        return value;
    }

    inline void outw(u16 port, u16 value) {
        asm volatile("outw %0, %1" : : "a"(value), "Nd"(port) : "memory");
    }

    inline u16 inw(u16 port) {
        u16 value = 0;

        asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port) : "memory");

        return value;
    }

    inline void outl(u16 port, u32 value) {
        asm volatile("outl %0, %1" : : "a"(value), "Nd"(port) : "memory");
    }

    inline u32 inl(u16 port) {
        u32 value = 0;

        asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port) : "memory");

        return value;
    }

    inline u64 rdmsr(u64 msr) {
        u32 lo = 0;
        u32 hi = 0;

        asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));

        return (static_cast<u64>(hi) << 32) | lo;
    }

    inline void wrmsr(u64 msr, u64 value) {
        const u32 lo = static_cast<u32>(value);
        const u32 hi = static_cast<u32>(value >> 32);

        asm volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
    }
} // namespace astia::arch::x86_64

namespace astia::arch::x86_64 {
    inline u8 pio_read8(void*, u16 port) {
        return inb(port);
    }

    inline u16 pio_read16(void*, u16 port) {
        return inw(port);
    }

    inline u32 pio_read32(void*, u16 port) {
        return inl(port);
    }

    inline void pio_write8(void*, u16 port, u8 value) {
        outb(port, value);
    }

    inline void pio_write16(void*, u16 port, u16 value) {
        outw(port, value);
    }

    inline void pio_write32(void*, u16 port, u32 value) {
        outl(port, value);
    }

    extern dev::pio_bus& device_pio;
} // namespace astia::arch::x86_64

namespace astia::arch {
    inline u8 pio::read8(u16 port) {
        return x86_64::device_pio.read8(port).value_or(0);
    }

    inline u16 pio::read16(u16 port) {
        return x86_64::device_pio.read16(port).value_or(0);
    }

    inline u32 pio::read32(u16 port) {
        return x86_64::device_pio.read32(port).value_or(0);
    }

    inline void pio::write8(u16 port, u8 value) {
        (void) x86_64::device_pio.write8(port, value);
    }

    inline void pio::write16(u16 port, u16 value) {
        (void) x86_64::device_pio.write16(port, value);
    }

    inline void pio::write32(u16 port, u32 value) {
        (void) x86_64::device_pio.write32(port, value);
    }
} // namespace astia::arch
