#include "initrd.hpp"
#include "test.hpp"

#include <cstring>
#include <vector>

extern unsigned char initrd_newc_blob[];
extern unsigned int  initrd_newc_blob_len;
extern unsigned char initrd_crc_blob[];
extern unsigned int  initrd_crc_blob_len;
extern unsigned char initrd_gzip_blob[];
extern unsigned int  initrd_gzip_blob_len;
extern unsigned char initrd_zstd_blob[];
extern unsigned int  initrd_zstd_blob_len;

namespace {
    struct collect {
        const char*  names[4];
        astia::usize n = 0;
    };

    bool collect_visit(void* ctx, const astia::initrd::file& f) {
        auto* c = static_cast<collect*>(ctx);

        if (c->n < 4)
            c->names[c->n] = f.name;

        ++c->n;

        return true;
    }

    bool stop_visit(void* ctx, const astia::initrd::file&) {
        ++*static_cast<int*>(ctx);

        return false; // ask the walk to stop after the first file
    }
} // namespace

ASTIA_TEST(initrd_detects_newc_and_crc) {
    astia::initrd::archive newc;
    astia::initrd::archive crc;

    ASTIA_CHECK(astia::initrd::open(initrd_newc_blob, initrd_newc_blob_len, newc));
    ASTIA_CHECK(astia::initrd::open(initrd_crc_blob, initrd_crc_blob_len, crc));
    ASTIA_CHECK(newc.format == astia::initrd::archive_format::cpio_newc);
    ASTIA_CHECK(crc.format == astia::initrd::archive_format::cpio_crc);
    ASTIA_CHECK(astia::initrd::count(newc) == 3);
    ASTIA_CHECK(astia::initrd::count(crc) == 3);
}

ASTIA_TEST(initrd_detects_compressed_containers) {
    ASTIA_CHECK(astia::initrd::detect_container(initrd_gzip_blob, initrd_gzip_blob_len) ==
                astia::initrd::archive_container::gzip);
    ASTIA_CHECK(astia::initrd::detect_container(initrd_zstd_blob, initrd_zstd_blob_len) ==
                astia::initrd::archive_container::zstd);

    astia::initrd::archive a;

    ASTIA_CHECK(!astia::initrd::open(initrd_gzip_blob, initrd_gzip_blob_len, a));
    ASTIA_CHECK(!astia::initrd::open(initrd_zstd_blob, initrd_zstd_blob_len, a));
}

ASTIA_TEST(initrd_reads_cpio_files) {
    astia::initrd::archive a;
    astia::initrd::file    f;

    ASTIA_CHECK(astia::initrd::open(initrd_newc_blob, initrd_newc_blob_len, a));
    ASTIA_CHECK(astia::initrd::find(a, "alpha.txt", f));
    ASTIA_CHECK(f.size == 6);
    ASTIA_CHECK(std::memcmp(f.data, "alpha\n", 6) == 0);
    ASTIA_CHECK(astia::initrd::find(a, "sub/gamma.txt", f));
    ASTIA_CHECK(f.size == 6);
    ASTIA_CHECK(std::memcmp(f.data, "gamma\n", 6) == 0);
}

ASTIA_TEST(initrd_counts_regular_files_only) {
    astia::initrd::archive a;

    ASTIA_CHECK(astia::initrd::open(initrd_newc_blob, initrd_newc_blob_len, a));
    // alpha.txt, beta.bin, sub/gamma.txt — the `sub/` directory entry is not a
    // regular file and must not be counted.
    ASTIA_CHECK(astia::initrd::count(a) == 3);
}

ASTIA_TEST(initrd_finds_files_and_their_bytes) {
    astia::initrd::archive a;

    ASTIA_CHECK(astia::initrd::open(initrd_newc_blob, initrd_newc_blob_len, a));

    astia::initrd::file f;

    ASTIA_CHECK(astia::initrd::find(a, "alpha.txt", f));
    ASTIA_CHECK(f.size == 6);
    ASTIA_CHECK(std::memcmp(f.data, "alpha\n", 6) == 0);

    ASTIA_CHECK(astia::initrd::find(a, "beta.bin", f));
    ASTIA_CHECK(f.size == 3);

    const unsigned char expect[] = {0xde, 0xad, 0xbe};

    ASTIA_CHECK(std::memcmp(f.data, expect, 3) == 0);

    ASTIA_CHECK(astia::initrd::find(a, "sub/gamma.txt", f));
    ASTIA_CHECK(f.size == 6);
    ASTIA_CHECK(std::memcmp(f.data, "gamma\n", 6) == 0);

    ASTIA_CHECK(!astia::initrd::find(a, "nope.txt", f));
}

ASTIA_TEST(initrd_crc_round_trips_binary_and_nested_files) {
    astia::initrd::archive a;
    astia::initrd::file    f;

    ASTIA_CHECK(astia::initrd::open(initrd_crc_blob, initrd_crc_blob_len, a));
    ASTIA_CHECK(astia::initrd::find(a, "beta.bin", f));

    const unsigned char expect[] = {0xde, 0xad, 0xbe};

    ASTIA_CHECK(f.size == sizeof(expect));
    ASTIA_CHECK(std::memcmp(f.data, expect, sizeof(expect)) == 0);
    ASTIA_CHECK(astia::initrd::find(a, "sub/gamma.txt", f));
    ASTIA_CHECK(f.size == 6);
    ASTIA_CHECK(std::memcmp(f.data, "gamma\n", 6) == 0);
}

ASTIA_TEST(initrd_enumerates_in_archive_order) {
    astia::initrd::archive a;

    ASTIA_CHECK(astia::initrd::open(initrd_newc_blob, initrd_newc_blob_len, a));

    collect c;

    ASTIA_CHECK(astia::initrd::each(a, collect_visit, &c));
    ASTIA_CHECK(c.n == 3);
    ASTIA_CHECK(std::strcmp(c.names[0], "alpha.txt") == 0);
    ASTIA_CHECK(std::strcmp(c.names[1], "beta.bin") == 0);
    ASTIA_CHECK(std::strcmp(c.names[2], "sub/gamma.txt") == 0);
}

ASTIA_TEST(initrd_visit_stops_on_false) {
    astia::initrd::archive a;

    ASTIA_CHECK(astia::initrd::open(initrd_newc_blob, initrd_newc_blob_len, a));

    int seen = 0;

    ASTIA_CHECK(!astia::initrd::each(a, stop_visit, &seen));
    ASTIA_CHECK(seen == 1);
}

ASTIA_TEST(initrd_validate_requires_the_trailer) {
    astia::initrd::archive a;

    ASTIA_CHECK(astia::initrd::open(initrd_newc_blob, initrd_newc_blob_len, a));
    ASTIA_CHECK(astia::initrd::validate(a));

    ASTIA_CHECK(astia::initrd::open(initrd_newc_blob, 128, a));
    ASTIA_CHECK(!astia::initrd::validate(a));
}

ASTIA_TEST(initrd_rejects_garbage) {
    unsigned char junk[600];

    std::memset(junk, 0xAB, sizeof(junk));

    astia::initrd::archive a;

    ASTIA_CHECK(!astia::initrd::open(junk, sizeof(junk), a));
}

ASTIA_TEST(initrd_rejects_tar) {
    unsigned char tar[512] = {};

    std::memcpy(tar + 257, "ustar", 5);

    astia::initrd::archive a;

    ASTIA_CHECK(!astia::initrd::open(tar, sizeof(tar), a));
}

ASTIA_TEST(initrd_rejects_short_input) {
    astia::initrd::archive a;

    ASTIA_CHECK(!astia::initrd::open(initrd_newc_blob, 100, a));
    ASTIA_CHECK(!astia::initrd::open(nullptr, 4096, a));
}

ASTIA_TEST(initrd_rejects_malformed_cpio_name) {
    std::vector<unsigned char> image(initrd_newc_blob,
                                     initrd_newc_blob + initrd_newc_blob_len);

    image[94]  = '0';
    image[95]  = '0';
    image[96]  = '0';
    image[97]  = '0';
    image[98]  = '0';
    image[99]  = '0';
    image[100] = '0';
    image[101] = '0';

    astia::initrd::archive a;

    ASTIA_CHECK(!astia::initrd::open(image.data(), image.size(), a));

    image.assign(initrd_newc_blob, initrd_newc_blob + initrd_newc_blob_len);

    image[119] = 'x';

    ASTIA_CHECK(!astia::initrd::open(image.data(), image.size(), a));
}

ASTIA_TEST(initrd_trailer_name_comparison_is_length_bounded) {
    std::vector<unsigned char> image(initrd_newc_blob,
                                     initrd_newc_blob + initrd_newc_blob_len);

    constexpr char trailer[] = "TRAILER!!!";

    astia::usize trailer_offset = image.size();

    for (astia::usize i = 0; i + sizeof(trailer) <= image.size(); ++i) {
        if (std::memcmp(image.data() + i, trailer, sizeof(trailer)) == 0) {
            trailer_offset = i;

            break;
        }
    }

    ASTIA_CHECK(trailer_offset != image.size());

    if (trailer_offset == image.size())
        return;

    astia::initrd::archive a;

    ASTIA_CHECK(astia::initrd::open(image.data(), image.size(), a));
    ASTIA_CHECK(astia::initrd::each(a, nullptr, nullptr));

    image[trailer_offset + 7] = 0;

    ASTIA_CHECK(astia::initrd::open(image.data(), image.size(), a));
    ASTIA_CHECK(!astia::initrd::each(a, nullptr, nullptr));

    image.assign(initrd_newc_blob, initrd_newc_blob + initrd_newc_blob_len);

    const astia::usize header_offset = trailer_offset - 110;

    std::memcpy(image.data() + header_offset + 94, "0000000c", 8);
    ASTIA_CHECK(astia::initrd::open(image.data(), image.size(), a));
    ASTIA_CHECK(!astia::initrd::each(a, nullptr, nullptr));
}

ASTIA_TEST(initrd_skips_nonregular_cpio_types) {
    std::vector<unsigned char> image(initrd_newc_blob,
                                     initrd_newc_blob + initrd_newc_blob_len);

    image[14] = '0';
    image[15] = '0';
    image[16] = '0';
    image[17] = '0';
    image[18] = '0';
    image[19] = '0';
    image[20] = '0';
    image[21] = '0';

    astia::initrd::archive a;

    ASTIA_CHECK(astia::initrd::open(image.data(), image.size(), a));
    ASTIA_CHECK(astia::initrd::count(a) == 2);
}

ASTIA_TEST(initrd_rejects_truncated_cpio) {
    astia::initrd::archive a;

    ASTIA_CHECK(astia::initrd::open(initrd_newc_blob, 128, a));

    collect c;

    ASTIA_CHECK(!astia::initrd::each(a, collect_visit, &c));
}

ASTIA_TEST(initrd_rejects_crc_mismatch) {
    std::vector<unsigned char> image(initrd_crc_blob,
                                     initrd_crc_blob + initrd_crc_blob_len);

    image[120] ^= 1;

    astia::initrd::archive a;

    ASTIA_CHECK(!astia::initrd::open(image.data(), image.size(), a));
}
