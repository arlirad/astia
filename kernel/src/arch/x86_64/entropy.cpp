#include <astia/arch/entropy.hpp>

#include "io.hpp"

namespace astia::arch {
    namespace {
        struct cpu_random_capabilities {
            bool rdrand;
            bool rdseed;
        };

        cpu_random_capabilities cpu_random() {
            cpu_random_capabilities capabilities{};

            u32 eax = 1;
            u32 ebx = 0;
            u32 ecx = 0;
            u32 edx = 0;

            asm volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));

            capabilities.rdrand = (ecx & (1u << 30)) != 0;

            eax = 7;
            ebx = 0;
            ecx = 0;
            edx = 0;

            asm volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));

            capabilities.rdseed = (ebx & (1u << 18)) != 0;

            return capabilities;
        }

        bool rdseed64(u64& value) {
            u8 success = 0;

            asm volatile("rdseed %0; setc %1" : "=r"(value), "=qm"(success));

            return success != 0;
        }

        bool rdrand64(u64& value) {
            u8 success = 0;

            asm volatile("rdrand %0; setc %1" : "=r"(value), "=qm"(success));

            return success != 0;
        }

        u8 cmos_read(u8 index) {
            x86_64::outb(0x70, index);

            return x86_64::inb(0x71);
        }

        u64 rtc_stamp() {
            for (u32 attempt = 0; attempt < 100000; ++attempt) {
                if ((cmos_read(0x0a) & 0x80) == 0)
                    break;
            }

            for (u32 attempt = 0; attempt < 4; ++attempt) {
                const u8 seconds = cmos_read(0x00);
                const u8 minutes = cmos_read(0x02);
                const u8 hours   = cmos_read(0x04);
                const u8 day     = cmos_read(0x07);
                const u8 month   = cmos_read(0x08);
                const u8 year    = cmos_read(0x09);

                if (cmos_read(0x00) != seconds)
                    continue;

                u64 stamp = seconds;

                stamp |= static_cast<u64>(minutes) << 8;
                stamp |= static_cast<u64>(hours) << 16;
                stamp |= static_cast<u64>(day) << 24;
                stamp |= static_cast<u64>(month) << 32;
                stamp |= static_cast<u64>(year) << 40;

                // An all-zero stamp means the RTC is absent or unreadable;
                // the jitter path must not treat that as entropy.
                return stamp == 0 ? 0 : stamp;
            }

            return 0;
        }
    } // namespace

    u32 hardware_random(u64* words, u32 capacity) {
        if (words == nullptr || capacity == 0)
            return 0;

        const cpu_random_capabilities capabilities = cpu_random();

        u32 count = 0;

        while (count < capacity) {
            u64  value = 0;
            bool ready = false;

            if (capabilities.rdseed)
                ready = rdseed64(value);

            if (!ready && capabilities.rdrand)
                ready = rdrand64(value);

            if (!ready)
                break;

            words[count++] = value;
        }

        return count;
    }

    u64 cycle_counter() {
        u32 low  = 0;
        u32 high = 0;

        asm volatile("rdtsc" : "=a"(low), "=d"(high));

        return (static_cast<u64>(high) << 32) | low;
    }

    bool wall_clock(u64& stamp) {
        const u64 value = rtc_stamp();
        if (value == 0)
            return false;

        stamp = value;

        return true;
    }
} // namespace astia::arch
