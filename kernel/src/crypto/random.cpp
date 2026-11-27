#include <astia/arch/entropy.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/crypto/csprng.hpp>
#include <astia/crypto/random.hpp>

namespace astia::crypto {
    namespace {
        core::spinlock lock;
        csprng         generator;

        struct seed_material {
            static constexpr u32 capacity = 24;

            u64 words[capacity]{};
            u32 count = 0;
        };

        void add(seed_material& material, u64 value) {
            if (material.count < seed_material::capacity)
                material.words[material.count++] = value;
        }

        /// Gather what the platform offers.  A single changing source is
        /// enough to key the generator, but the sources are combined so a
        /// weak one never decides the key on its own.
        bool gather(seed_material& material) {
            const u32 hardware =
                arch::hardware_random(material.words, seed_material::capacity);

            material.count += hardware;

            // Deterministic words below are extra mixing, never the reason
            // to consider the pool seeded.
            bool available = hardware != 0;

            const u64 start = arch::cycle_counter();
            u64       delta = 0;
            u64       prior = start;

            for (u32 i = 0; i < 32; ++i) {
                const u64 current = arch::cycle_counter();

                delta ^= current - prior;
                prior = current;
            }

            if (start != 0)
                available = true;

            add(material, start);
            add(material, delta);

            add(material, arch::timer_ticks());
            add(material, arch::timer_hz());

            u64 stamp = 0;

            if (arch::wall_clock(stamp)) {
                available = true;

                add(material, stamp);
            }

            add(material, reinterpret_cast<u64>(&material));
            add(material, reinterpret_cast<u64>(&generator));

            return available;
        }

        bool ensure_seeded_locked() {
            if (generator.seeded())
                return true;

            seed_material material;

            if (!gather(material))
                return false;

            generator.reseed(material.words, material.count * sizeof(u64));

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
