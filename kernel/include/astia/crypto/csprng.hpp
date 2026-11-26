#pragma once

#include <astia/common.hpp>

namespace astia::crypto {
    /// ChaCha20-based deterministic random bit generator.
    ///
    /// The generator keeps a 256-bit key, a 96-bit nonce, and a 32-bit block
    /// counter.  Seeding and reseeding run the ChaCha20 round function over
    /// the supplied material together with the previous key, so every derived
    /// state is a one-way function of the material.  Output is the ChaCha20
    /// keystream; the caller never observes the key or the counter.
    ///
    /// The class is deliberately lock-free and platform-free: the kernel
    /// wrapper in astia/crypto/random.hpp owns the global instance and its
    /// spinlock.  A generator that has not been seeded refuses to produce
    /// bytes instead of falling back to predictable output.
    class csprng {
        public:
        static constexpr u32 key_size   = 32;
        static constexpr u32 nonce_size = 12;
        static constexpr u32 block_size = 64;

        /// Upper bound on one getrandom()/device request, shared with the ABI.
        static constexpr u32 max_request = 1024 * 1024;

        csprng() = default;

        /// One ChaCha20 keystream block.  Public so the host test can check
        /// the RFC 8439 vector without reaching into private state.
        static void chacha20_block(const u8 key[key_size], const u8 nonce[nonce_size],
                                   u32 counter, u8 output[block_size]);

        /// Replace the key from `length` bytes of entropy material.
        void reseed(const void* material, usize length);

        /// Mix additional material into the current key without discarding it.
        /// Used for fork derivation and explicit reseeding.
        void stir(const void* material, usize length);

        /// Derive a child stream from the current state and a child identity.
        void fork_derive(u64 child_pid);

        bool seeded() const {
            return _seeded;
        }

        /// Fill exactly `length` bytes.  Returns false, and clears the output,
        /// while the generator has no seed.
        bool fill(void* output, usize length);

        private:
        void absorb(const void* material, usize length, u8 domain);

        u8   _key[key_size]{};
        u8   _nonce[nonce_size]{};
        u32  _counter = 0;
        u8   _block[block_size]{};
        u32  _block_used = block_size;
        bool _seeded     = false;
    };
} // namespace astia::crypto
