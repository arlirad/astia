#include <astia/crypto/csprng.hpp>

namespace astia::crypto {
    namespace {
        constexpr u32 state_words = 16;

        constexpr u32 load32(const u8* bytes) {
            return static_cast<u32>(bytes[0]) | static_cast<u32>(bytes[1]) << 8 |
                   static_cast<u32>(bytes[2]) << 16 | static_cast<u32>(bytes[3]) << 24;
        }

        constexpr void store32(u8* bytes, u32 value) {
            bytes[0] = static_cast<u8>(value);
            bytes[1] = static_cast<u8>(value >> 8);
            bytes[2] = static_cast<u8>(value >> 16);
            bytes[3] = static_cast<u8>(value >> 24);
        }

        constexpr u32 rotate_left(u32 value, u32 count) {
            return (value << count) | (value >> (32 - count));
        }

        void quarter_round(u32& a, u32& b, u32& c, u32& d) {
            a += b;
            d ^= a;
            d = rotate_left(d, 16);
            c += d;
            b ^= c;
            b = rotate_left(b, 12);
            a += b;
            d ^= a;
            d = rotate_left(d, 8);
            c += d;
            b ^= c;
            b = rotate_left(b, 7);
        }

        void rounds(u32 state[state_words]) {
            for (u32 i = 0; i < 10; ++i) {
                quarter_round(state[0], state[4], state[8], state[12]);
                quarter_round(state[1], state[5], state[9], state[13]);
                quarter_round(state[2], state[6], state[10], state[14]);
                quarter_round(state[3], state[7], state[11], state[15]);
                quarter_round(state[0], state[5], state[10], state[15]);
                quarter_round(state[1], state[6], state[11], state[12]);
                quarter_round(state[2], state[7], state[8], state[13]);
                quarter_round(state[3], state[4], state[9], state[14]);
            }
        }
    } // namespace

    void csprng::chacha20_block(const u8 key[key_size], const u8 nonce[nonce_size],
                                u32 counter, u8 output[block_size]) {
        u32 state[state_words] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574};

        for (u32 i = 0; i < 8; ++i) {
            state[4 + i] = load32(key + i * 4);
        }

        state[12] = counter;
        state[13] = load32(nonce);
        state[14] = load32(nonce + 4);
        state[15] = load32(nonce + 8);

        u32 working[state_words];

        for (u32 i = 0; i < state_words; ++i) {
            working[i] = state[i];
        }

        rounds(working);

        for (u32 i = 0; i < state_words; ++i) {
            store32(output + i * 4, working[i] + state[i]);
        }
    }

    void csprng::absorb(const void* material, usize length, u8 domain) {
        u32 state[state_words] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574};

        for (u32 i = 0; i < 8; ++i) {
            state[4 + i] = load32(_key + i * 4);
        }

        state[12] = _counter;
        state[13] = load32(_nonce);
        state[14] = load32(_nonce + 4);
        state[15] = load32(_nonce + 8);

        state[12] ^= static_cast<u32>(length) * 0x9e3779b9u;
        state[13] ^= static_cast<u32>(domain) * 0x85ebca6bu;

        const auto* bytes = static_cast<const u8*>(material);

        for (usize i = 0; i < length; ++i) {
            state[4 + (i % 12)] ^= static_cast<u32>(bytes[i]) << (8 * ((i / 12) % 4));
        }

        u32 working[state_words];

        for (u32 i = 0; i < state_words; ++i) {
            working[i] = state[i];
        }

        rounds(working);

        u8 output[block_size];

        for (u32 i = 0; i < state_words; ++i) {
            store32(output + i * 4, working[i] + state[i]);
        }

        for (u32 i = 0; i < 8; ++i) {
            store32(_key + i * 4, load32(output + i * 4));
        }

        for (u32 i = 0; i < 3; ++i) {
            store32(_nonce + i * 4, load32(output + 32 + i * 4));
        }

        _counter    = load32(output + 44);
        _block_used = block_size;
        _seeded     = true;
    }

    void csprng::reseed(const void* material, usize length) {
        absorb(material, length, 0x52);
    }

    void csprng::stir(const void* material, usize length) {
        absorb(material, length, 0x53);
    }

    void csprng::fork_derive(u64 child_pid) {
        u8 material[sizeof child_pid + 1]{};

        material[0] = 0x46;

        for (u32 i = 0; i < sizeof child_pid; ++i) {
            material[1 + i] = static_cast<u8>(child_pid >> (8 * i));
        }

        stir(material, sizeof material);
    }

    bool csprng::fill(void* output, usize length) {
        auto* bytes = static_cast<u8*>(output);

        if (output == nullptr && length != 0)
            return false;

        if (!_seeded) {
            for (usize i = 0; i < length; ++i) {
                bytes[i] = 0;
            }

            return false;
        }

        usize done = 0;

        while (done < length) {
            if (_block_used == block_size) {
                chacha20_block(_key, _nonce, _counter, _block);
                ++_counter;

                _block_used = 0;
            }

            const usize available = block_size - _block_used;
            const usize count     = length - done < available ? length - done : available;

            for (usize i = 0; i < count; ++i) {
                bytes[done + i] = _block[_block_used + i];
            }

            done += count;
            _block_used += static_cast<u32>(count);
        }

        return true;
    }
} // namespace astia::crypto
