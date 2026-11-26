#include "decompress.hpp"
#include "initrd.hpp"
#include "test.hpp"

#include <cstring>
#include <vector>

extern unsigned char initrd_newc_blob[];
extern unsigned int  initrd_newc_blob_len;
extern unsigned char initrd_gzip_blob[];
extern unsigned int  initrd_gzip_blob_len;
extern unsigned char initrd_zstd_blob[];
extern unsigned int  initrd_zstd_blob_len;
extern unsigned char initrd_zstd_nosize_blob[];
extern unsigned int  initrd_zstd_nosize_blob_len;

namespace {
    using astia::initrd::archive_container;
    using astia::initrd::decode_error;
    using astia::initrd::decode_limits;

    struct decoded {
        std::vector<astia::u8> bytes;
        decode_error           error = decode_error::ok;
    };

    decoded decode_all(archive_container container, const unsigned char* data,
                       astia::usize size, decode_limits limits = {}) {
        decoded      result;
        astia::usize expected = 0;

        result.error = astia::initrd::decoded_size(
            container,
            astia::span<const astia::u8>(static_cast<const astia::u8*>(data), size),
            limits, expected);

        if (result.error != decode_error::ok)
            return result;

        result.bytes.resize(expected);

        result.error = astia::initrd::decode(
            container,
            astia::span<const astia::u8>(static_cast<const astia::u8*>(data), size),
            astia::span<astia::u8>(result.bytes.data(), result.bytes.size()), limits);

        return result;
    }

    std::vector<unsigned char> copy_blob(unsigned char* data, unsigned int size) {
        return std::vector<unsigned char>(data, data + size);
    }
} // namespace

ASTIA_TEST(initrd_decodes_gzip_and_zstd_to_the_raw_archive) {
    const decoded gzip =
        decode_all(archive_container::gzip, initrd_gzip_blob, initrd_gzip_blob_len);
    const decoded zstd =
        decode_all(archive_container::zstd, initrd_zstd_blob, initrd_zstd_blob_len);

    ASTIA_CHECK(gzip.error == decode_error::ok);
    ASTIA_CHECK(zstd.error == decode_error::ok);
    ASTIA_CHECK(gzip.bytes.size() == initrd_newc_blob_len);
    ASTIA_CHECK(zstd.bytes.size() == initrd_newc_blob_len);
    ASTIA_CHECK(std::memcmp(gzip.bytes.data(), initrd_newc_blob, initrd_newc_blob_len) ==
                0);
    ASTIA_CHECK(std::memcmp(zstd.bytes.data(), initrd_newc_blob, initrd_newc_blob_len) ==
                0);
}

ASTIA_TEST(initrd_decoded_archive_passes_full_validation) {
    const decoded gzip =
        decode_all(archive_container::gzip, initrd_gzip_blob, initrd_gzip_blob_len);

    ASTIA_CHECK(gzip.error == decode_error::ok);

    astia::initrd::archive archive;

    ASTIA_CHECK(astia::initrd::open(gzip.bytes.data(), gzip.bytes.size(), archive));
    ASTIA_CHECK(astia::initrd::validate(archive));
    ASTIA_CHECK(astia::initrd::count(archive) == 3);
}

ASTIA_TEST(initrd_rejects_truncated_compressed_input) {
    for (astia::usize cut = 1; cut <= 8; ++cut) {
        const decoded gzip =
            decode_all(archive_container::gzip, initrd_gzip_blob,
                       static_cast<astia::usize>(initrd_gzip_blob_len) - cut);
        const decoded zstd =
            decode_all(archive_container::zstd, initrd_zstd_blob,
                       static_cast<astia::usize>(initrd_zstd_blob_len) - cut);

        ASTIA_CHECK(gzip.error != decode_error::ok);
        ASTIA_CHECK(zstd.error != decode_error::ok);
    }
}

ASTIA_TEST(initrd_rejects_gzip_crc_mismatch) {
    std::vector<unsigned char> image = copy_blob(initrd_gzip_blob, initrd_gzip_blob_len);

    image[image.size() - 8] ^= 0x01;

    const decoded result =
        decode_all(archive_container::gzip, image.data(), image.size());

    ASTIA_CHECK(result.error == decode_error::checksum);
}

ASTIA_TEST(initrd_rejects_zstd_checksum_mismatch) {
    std::vector<unsigned char> image = copy_blob(initrd_zstd_blob, initrd_zstd_blob_len);

    image[image.size() - 1] ^= 0x01;

    const decoded result =
        decode_all(archive_container::zstd, image.data(), image.size());

    ASTIA_CHECK(result.error == decode_error::checksum);
}

ASTIA_TEST(initrd_rejects_zstd_trailing_data) {
    std::vector<unsigned char> image = copy_blob(initrd_zstd_blob, initrd_zstd_blob_len);

    image.push_back(0x5a);

    const decoded result =
        decode_all(archive_container::zstd, image.data(), image.size());

    ASTIA_CHECK(result.error == decode_error::trailing_data);
}

ASTIA_TEST(initrd_rejects_gzip_trailing_data) {
    std::vector<unsigned char> image = copy_blob(initrd_gzip_blob, initrd_gzip_blob_len);

    // Keep the real trailer in place and insert bytes between the DEFLATE
    // stream and it.
    image.insert(image.end() - 8, 0x5a);

    const decoded result =
        decode_all(archive_container::gzip, image.data(), image.size());

    ASTIA_CHECK(result.error == decode_error::trailing_data);
}

ASTIA_TEST(initrd_rejects_zstd_without_content_size) {
    astia::usize       expected = 0;
    const decode_error error    = astia::initrd::decoded_size(
        archive_container::zstd,
        astia::span<const astia::u8>(
            static_cast<const astia::u8*>(initrd_zstd_nosize_blob),
            static_cast<astia::usize>(initrd_zstd_nosize_blob_len)),
        decode_limits{}, expected);

    ASTIA_CHECK(error == decode_error::unsupported);
}

ASTIA_TEST(initrd_enforces_decode_limits) {
    decode_limits limits;

    limits.max_output = 16;

    ASTIA_CHECK(decode_all(archive_container::gzip, initrd_gzip_blob,
                           initrd_gzip_blob_len, limits)
                    .error == decode_error::output_limit);
    ASTIA_CHECK(decode_all(archive_container::zstd, initrd_zstd_blob,
                           initrd_zstd_blob_len, limits)
                    .error == decode_error::output_limit);

    limits = {};

    limits.max_input = 16;

    ASTIA_CHECK(decode_all(archive_container::gzip, initrd_gzip_blob,
                           initrd_gzip_blob_len, limits)
                    .error == decode_error::input_limit);

    limits             = {};
    limits.max_ratio   = 1;
    limits.ratio_floor = 0;

    ASTIA_CHECK(decode_all(archive_container::gzip, initrd_gzip_blob,
                           initrd_gzip_blob_len, limits)
                    .error == decode_error::ratio_limit);
    ASTIA_CHECK(decode_all(archive_container::zstd, initrd_zstd_blob,
                           initrd_zstd_blob_len, limits)
                    .error == decode_error::ratio_limit);

    limits            = {};
    limits.max_window = 1;

    ASTIA_CHECK(decode_all(archive_container::zstd, initrd_zstd_blob,
                           initrd_zstd_blob_len, limits)
                    .error == decode_error::window_limit);
}

ASTIA_TEST(initrd_decode_requires_the_declared_output_size) {
    astia::usize       expected = 0;
    const decode_error error    = astia::initrd::decoded_size(
        archive_container::gzip,
        astia::span<const astia::u8>(static_cast<const astia::u8*>(initrd_gzip_blob),
                                        static_cast<astia::usize>(initrd_gzip_blob_len)),
        decode_limits{}, expected);

    ASTIA_CHECK(error == decode_error::ok);
    ASTIA_CHECK(expected > 1);

    std::vector<astia::u8> short_output(expected - 1);

    ASTIA_CHECK(
        astia::initrd::decode(
            archive_container::gzip,
            astia::span<const astia::u8>(static_cast<const astia::u8*>(initrd_gzip_blob),
                                         static_cast<astia::usize>(initrd_gzip_blob_len)),
            astia::span<astia::u8>(short_output.data(), short_output.size()),
            decode_limits{}) == decode_error::malformed);
}

ASTIA_TEST(initrd_rejects_malformed_gzip_framing) {
    std::vector<unsigned char> image = copy_blob(initrd_gzip_blob, initrd_gzip_blob_len);

    image[2] = 9;

    ASTIA_CHECK(decode_all(archive_container::gzip, image.data(), image.size()).error ==
                decode_error::malformed);

    image = copy_blob(initrd_gzip_blob, initrd_gzip_blob_len);
    image[3] |= 0x20; // reserved flag bit

    ASTIA_CHECK(decode_all(archive_container::gzip, image.data(), image.size()).error ==
                decode_error::unsupported);

    image = copy_blob(initrd_gzip_blob, initrd_gzip_blob_len);
    image[3] |= 0x04; // FEXTRA with no length field

    ASTIA_CHECK(decode_all(archive_container::gzip, image.data(), image.size()).error ==
                decode_error::truncated);
}

ASTIA_TEST(initrd_rejects_zstd_skippable_frame) {
    const astia::u8 frame[]  = {0x50, 0x2a, 0x4d, 0x18, 0, 0, 0, 0};
    astia::usize    expected = 0;

    ASTIA_CHECK(astia::initrd::detect_container(frame, sizeof(frame)) ==
                archive_container::zstd_skippable);
    ASTIA_CHECK(astia::initrd::decoded_size(
                    archive_container::zstd_skippable,
                    astia::span<const astia::u8>(frame, sizeof(frame)), decode_limits{},
                    expected) == decode_error::unsupported);
}

ASTIA_TEST(initrd_rejects_raw_container_for_compressed_decoder) {
    astia::usize expected = 0;

    ASTIA_CHECK(
        astia::initrd::decoded_size(
            archive_container::raw,
            astia::span<const astia::u8>(static_cast<const astia::u8*>(initrd_newc_blob),
                                         static_cast<astia::usize>(initrd_newc_blob_len)),
            decode_limits{}, expected) == decode_error::unsupported);
}
