#include <astia/arch/entropy.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/crypto/csprng.hpp>
#include <astia/crypto/entropy_pool.hpp>
#include <astia/crypto/random.hpp>

namespace astia::crypto {
    namespace {
        core::spinlock lock;
        csprng         generator;

        constexpr u32 hardware_capacity = 24;

        /// Collect the platform sources and mix them through the pool.
        ///
        /// Policy: only a strong source makes the pool ready.  A hardware
        /// RNG, a bootloader seed, or a platform clock is strong; cycle
        /// counter deltas, timer ticks, and addresses are weak by
        /// construction.  Weak material is still mixed so it adds variance,
        /// but a platform that offers only weak material stays unseeded and
        /// callers get EAGAIN instead of predictable keys.
        bool gather(entropy_pool& pool) {
            u64 words[hardware_capacity]{};

            const u32 hardware = arch::hardware_random(words, hardware_capacity);

            pool.add_words(entropy_quality::strong, words, hardware);

            const u64 start = arch::cycle_counter();
            u64       delta = 0;
            u64       prior = start;

            for (u32 i = 0; i < 32; ++i) {
                const u64 current = arch::cycle_counter();

                delta ^= current - prior;
                prior = current;
            }

            pool.add_word(entropy_quality::weak, start);
            pool.add_word(entropy_quality::weak, delta);
            pool.add_word(entropy_quality::weak, arch::timer_ticks());
            pool.add_word(entropy_quality::weak, arch::timer_hz());

            u64 stamp = 0;

            if (arch::wall_clock(stamp))
                pool.add_word(entropy_quality::strong, stamp);

            pool.add_word(entropy_quality::weak, reinterpret_cast<u64>(&pool));
            pool.add_word(entropy_quality::weak, reinterpret_cast<u64>(&generator));

            return pool.ready();
        }

        bool ensure_seeded_locked() {
            if (generator.seeded())
                return true;

            entropy_pool pool;

            if (!gather(pool))
                return false;

            u8 seed[entropy_pool::seed_size];

            if (!pool.copy(seed))
                return false;

            generator.reseed(seed, sizeof seed);

            for (u32 i = 0; i < sizeof seed; ++i) {
                seed[i] = 0;
            }

            return generator.seeded();
        }
    } // namespace

    bool seeded() {
        core::spinlock_guard guard(lock);

        return generator.seeded();
    }

    bool random_bytes(void* buffer, usz length) {
        if (buffer == nullptr && length != 0)
            return false;

        if (length == 0)
            return true;

        core::spinlock_guard guard(lock);

        if (!ensure_seeded_locked())
            return false;

        return generator.fill(buffer, length);
    }

    void fork_child(u64 child_pid) {
        core::spinlock_guard guard(lock);

        if (!generator.seeded())
            return;

        generator.fork_derive(child_pid);
    }
} // namespace astia::crypto
