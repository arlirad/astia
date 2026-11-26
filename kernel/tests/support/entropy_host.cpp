#include <astia/arch/entropy.hpp>
#include <astia/arch/interrupts.hpp>

namespace astia::arch {
    namespace {
        astia::u64 counter = 0;

        astia::u64 next() {
            // Deterministic but changing material: host tests need the DRBG
            // to seed, not real unpredictability.
            ++counter;

            return counter * 0x9e3779b97f4a7c15ull;
        }
    } // namespace

    u32 hardware_random(u64* words, u32 capacity) {
        if (words == nullptr)
            return 0;

        for (u32 i = 0; i < capacity; ++i) {
            words[i] = next();
        }

        return capacity;
    }

    u64 cycle_counter() {
        return next();
    }

    bool wall_clock(u64& stamp) {
        stamp = next();

        return true;
    }

    u64 timer_ticks() {
        return counter;
    }

    u32 timer_hz() {
        return 1000;
    }
} // namespace astia::arch
