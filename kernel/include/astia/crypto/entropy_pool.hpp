#pragma once

#include <astia/common.hpp>

namespace astia::crypto {
    /// How much an entropy contribution can be trusted.
    ///
    /// `strong` material varies in a way an outside observer cannot model: a
    /// hardware RNG (RDRAND/RDSEED), a bootloader-provided seed, or a
    /// platform clock reading that carries real per-boot variation.
    ///
    /// `weak` material varies but is observable or replayable: cycle-counter
    /// samples, timer ticks, addresses, and similar jitter.  It is mixed for
    /// defence in depth and never makes the pool seedable on its own.
    enum class entropy_quality : u8 {
        weak,
        strong,
    };

    /// Mixing and health policy for the kernel CSPRNG seed.
    ///
    /// Every contribution is folded into a 256-bit state with a keyed
    /// ChaCha20 compression, so later material is a one-way function of
    /// everything already absorbed.  A weak source therefore contributes but
    /// can neither cancel nor dominate a strong one.
    ///
    /// The pool becomes seedable only after at least `min_strong_words`
    /// nonzero strong words.  An all-zero strong contribution (an absent RNG
    /// or an unwritten boot seed) is ignored, so "no source" can never look
    /// like entropy.  A platform that offers only weak material leaves the
    /// CSPRNG unseeded and callers get EAGAIN; it never falls back to
    /// predictable output.
    class entropy_pool {
        public:
        static constexpr u32 seed_size        = 32;
        static constexpr u32 min_strong_words = 1;

        entropy_pool();

        void add_word(entropy_quality quality, u64 value);

        void add_words(entropy_quality quality, const u64* words, u32 count);

        void add_bytes(entropy_quality quality, const void* bytes, usz length);

        u32 strong_words() const {
            return _strong_words;
        }

        u32 weak_words() const {
            return _weak_words;
        }

        bool ready() const {
            return _strong_words >= min_strong_words;
        }

        /// Copy the mixed seed.  Returns false and clears `output` until the
        /// pool is ready.
        bool copy(u8 output[seed_size]) const;

        private:
        void absorb(const u8* bytes, usz length, u8 domain);

        u8  _key[seed_size];
        u32 _blocks        = 0;
        u32 _contributions = 0;
        u32 _strong_words  = 0;
        u32 _weak_words    = 0;
    };
} // namespace astia::crypto
