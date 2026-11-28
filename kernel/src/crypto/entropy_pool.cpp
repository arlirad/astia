#include <astia/crypto/csprng.hpp>
#include <astia/crypto/entropy_pool.hpp>

namespace astia::crypto {
    namespace {
        constexpr u8 strong_domain = 0x53;
        constexpr u8 weak_domain   = 0x57;
        constexpr u8 fold_domain   = 0xa5;

        constexpr u8 initial_key[entropy_pool::seed_size] = {
            0x41, 0x73, 0x74, 0x69, 0x61, 0x43, 0x53, 0x50, 0x52, 0x4e, 0x47,
            0x53, 0x65, 0x65, 0x64, 0x2d, 0x76, 0x31, 0x9e, 0x37, 0x79, 0xb9,
            0x7f, 0x4a, 0x7c, 0x15, 0x85, 0xeb, 0xca, 0x6b, 0x33, 0x5d};
    } // namespace

    entropy_pool::entropy_pool() {
        for (u32 i = 0; i < seed_size; ++i) {
            _key[i] = initial_key[i];
        }
    }

    void entropy_pool::absorb(const u8* bytes, usz length, u8 domain) {
        u8 header[csprng::nonce_size]{};

        header[0] = domain;
        header[1] = static_cast<u8>(_contributions);
        header[2] = static_cast<u8>(_contributions >> 8);
        header[3] = static_cast<u8>(_contributions >> 16);
        header[4] = static_cast<u8>(length);
        header[5] = static_cast<u8>(length >> 8);
        header[6] = static_cast<u8>(length >> 16);

        u8 folded[csprng::block_size];

        csprng::chacha20_block(_key, header, _blocks, folded);
        ++_blocks;

        for (usz i = 0; i < length; ++i) {
            folded[i % sizeof folded] ^= bytes[i];
        }

        header[11] = fold_domain;

        u8 compressed[csprng::block_size];

        csprng::chacha20_block(folded, header, _blocks, compressed);
        ++_blocks;

        for (u32 i = 0; i < seed_size; ++i) {
            _key[i] = compressed[i];
        }

        ++_contributions;
    }

    void entropy_pool::add_word(entropy_quality quality, u64 value) {
        u8 bytes[sizeof value];

        for (usz i = 0; i < sizeof value; ++i) {
            bytes[i] = static_cast<u8>(value >> (8 * i));
        }

        add_bytes(quality, bytes, sizeof bytes);
    }

    void entropy_pool::add_words(entropy_quality quality, const u64* words, u32 count) {
        if (words == nullptr)
            return;

        for (u32 i = 0; i < count; ++i) {
            add_word(quality, words[i]);
        }
    }

    void entropy_pool::add_bytes(entropy_quality quality, const void* bytes, usz length) {
        if (bytes == nullptr || length == 0)
            return;

        const auto* data = static_cast<const u8*>(bytes);

        bool nonzero = false;

        for (usz i = 0; i < length; ++i) {
            if (data[i] != 0) {
                nonzero = true;

                break;
            }
        }

        if (quality == entropy_quality::strong && !nonzero)
            return;

        absorb(data, length,
               quality == entropy_quality::strong ? strong_domain : weak_domain);

        const u32 words = static_cast<u32>((length + 7) / 8);

        if (quality == entropy_quality::strong)
            _strong_words += words;
        else
            _weak_words += words;
    }

    bool entropy_pool::copy(u8 output[seed_size]) const {
        if (output == nullptr)
            return false;

        if (!ready()) {
            for (u32 i = 0; i < seed_size; ++i) {
                output[i] = 0;
            }

            return false;
        }

        for (u32 i = 0; i < seed_size; ++i) {
            output[i] = _key[i];
        }

        return true;
    }
} // namespace astia::crypto
