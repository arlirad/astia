#include "initrd.hpp"
#include "test.hpp"

#include <cstring>

// The ustar reader, exercised against a tarball built by tests/make_initrd.sh
// at build time and byte-embedded, so parsing sees genuine GNU tar output:
// three regular files (one of them binary) plus a `sub/` directory entry that
// must be skipped.

extern unsigned char initrd_blob[];
extern unsigned int  initrd_blob_len;

namespace {

    struct collect {
        const char*  names[4];
        astia::usize n = 0;
    };

    bool collect_visit(void* ctx, const astia::initrd::file& f) {
        auto* c = static_cast<collect*>(ctx);
        if (c->n < 4) {
            c->names[c->n] = f.name;
        }
        ++c->n;
        return true;
    }

    bool stop_visit(void* ctx, const astia::initrd::file&) {
        ++*static_cast<int*>(ctx);
        return false; // ask the walk to stop after the first file
    }

} // namespace

ASTIA_TEST(initrd_opens_real_tarball) {
    astia::initrd::archive a;
    ASTIA_CHECK(astia::initrd::open(initrd_blob, initrd_blob_len, a));
    ASTIA_CHECK(static_cast<const void*>(a.base) ==
                static_cast<const void*>(initrd_blob));
    ASTIA_CHECK(a.len == initrd_blob_len);
}

ASTIA_TEST(initrd_counts_regular_files_only) {
    astia::initrd::archive a;
    ASTIA_CHECK(astia::initrd::open(initrd_blob, initrd_blob_len, a));
    // alpha.txt, beta.bin, sub/gamma.txt — the `sub/` directory entry is not a
    // regular file and must not be counted.
    ASTIA_CHECK(astia::initrd::count(a) == 3);
}

ASTIA_TEST(initrd_finds_files_and_their_bytes) {
    astia::initrd::archive a;
    ASTIA_CHECK(astia::initrd::open(initrd_blob, initrd_blob_len, a));

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

ASTIA_TEST(initrd_enumerates_in_archive_order) {
    astia::initrd::archive a;
    ASTIA_CHECK(astia::initrd::open(initrd_blob, initrd_blob_len, a));
    collect c;
    ASTIA_CHECK(astia::initrd::each(a, collect_visit, &c));
    ASTIA_CHECK(c.n == 3);
    ASTIA_CHECK(std::strcmp(c.names[0], "alpha.txt") == 0);
    ASTIA_CHECK(std::strcmp(c.names[1], "beta.bin") == 0);
    ASTIA_CHECK(std::strcmp(c.names[2], "sub/gamma.txt") == 0);
}

ASTIA_TEST(initrd_visit_stops_on_false) {
    astia::initrd::archive a;
    ASTIA_CHECK(astia::initrd::open(initrd_blob, initrd_blob_len, a));
    int seen = 0;
    ASTIA_CHECK(!astia::initrd::each(a, stop_visit, &seen));
    ASTIA_CHECK(seen == 1);
}

ASTIA_TEST(initrd_accepts_empty_archive) {
    const unsigned char    zeros[1024] = {};
    astia::initrd::archive a;
    ASTIA_CHECK(astia::initrd::open(zeros, sizeof(zeros), a));
    ASTIA_CHECK(astia::initrd::count(a) == 0);
}

ASTIA_TEST(initrd_rejects_garbage) {
    unsigned char junk[600];
    std::memset(junk, 0xAB, sizeof(junk));
    astia::initrd::archive a;
    ASTIA_CHECK(!astia::initrd::open(junk, sizeof(junk), a));
}

ASTIA_TEST(initrd_rejects_short_input) {
    astia::initrd::archive a;
    ASTIA_CHECK(!astia::initrd::open(initrd_blob, 100, a));
    ASTIA_CHECK(!astia::initrd::open(nullptr, 4096, a));
}

ASTIA_TEST(initrd_rejects_truncated_file_data) {
    astia::initrd::archive a;
    // Cut the image just inside alpha.txt's data block: the header promises 6
    // bytes but only 3 remain, so the walk must fail rather than read past the
    // archive (and report the file it never fully saw).
    ASTIA_CHECK(astia::initrd::open(initrd_blob, 515, a));
    collect c;
    ASTIA_CHECK(!astia::initrd::each(a, collect_visit, &c));
    ASTIA_CHECK(c.n == 0);
}
