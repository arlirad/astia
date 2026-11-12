// ustar tar and newc/cpio-crc readers.
//
// Header field offsets are the POSIX ustar layout (identical for the GNU
// variant, which only differs in the magic version bytes and in supporting
// base-256 numeric fields, which we reject — a simple initrd does not need
// them).

#include "initrd.hpp"

namespace astia::initrd {

    namespace {

        // ustar header field offsets (bytes).
        constexpr usize off_name     = 0;
        constexpr usize off_size     = 124;
        constexpr usize off_typeflag = 156;
        constexpr usize off_magic    = 257;
        constexpr usize name_len     = name_field_size;
        constexpr usize size_len     = size_field_size;

        // typeflag values we care about.
        constexpr char type_regular     = '0';
        constexpr char type_regular_old = '\0'; // pre-POSIX regular file

        constexpr usize cpio_header_size = 110;
        constexpr char  cpio_newc_magic[] = "070701";
        constexpr char  cpio_crc_magic[]  = "070702";
        constexpr u8    gzip_magic_0      = 0x1f;
        constexpr u8    gzip_magic_1      = 0x8b;
        constexpr u8    zstd_magic_0      = 0x28;
        constexpr u8    zstd_magic_1      = 0xb5;
        constexpr u8    zstd_magic_2      = 0x2f;
        constexpr u8    zstd_magic_3      = 0xfd;
        constexpr u32   cpio_type_mask    = 0170000;
        constexpr u32   cpio_type_regular = 0100000;
        constexpr char  cpio_trailer[]    = "TRAILER!!!";

        struct cpio_entry {
            const u8* name = nullptr;
            const u8* data = nullptr;
            usize     size = 0;
            u32       mode = 0;
            u32       check = 0;
            usize     next = 0;
        };

        bool str_eq(const char* a, const char* b) {
            while (*a != '\0' && *a == *b) {
                ++a;
                ++b;
            }
            return *a == *b;
        }

        // True if `p` begins with the five-byte ustar magic. Both POSIX and GNU
        // spell it "ustar"; the bytes that follow differ (" \0" vs "\0 "), so we
        // compare exactly the five-byte marker rather than treating it as a C string.
        bool has_ustar_magic(const u8* p) {
            for (usize i = 0; i < ustar_magic_size; ++i) {
                if (p[i] != static_cast<u8>(ustar_magic[i])) {
                    return false;
                }
            }
            return true;
        }

        // True if [p, p+n) contains a NUL, i.e. a fixed field is terminated.
        bool has_nul(const u8* p, usize n) {
            for (usize i = 0; i < n; ++i) {
                if (p[i] == '\0') {
                    return true;
                }
            }
            return false;
        }

        // Parse an octal ASCII field (tar stores sizes as octal text). Stops at
        // the first NUL or space terminator; rejects anything else, including
        // base-256 encoded fields.
        bool parse_octal(const char* s, usize n, u64& out) {
            u64 v = 0;
            for (usize i = 0; i < n; ++i) {
                char c = s[i];
                if (c == '\0' || c == ' ') {
                    break; // terminator; ignore trailing padding
                }
                if (c < '0' || c > '7') {
                    return false;
                }
                if (v > (~u64{0} >> 3)) {
                    return false;
                }
                v = (v << 3) + static_cast<u64>(c - '0');
            }
            out = v;
            return true;
        }

        // True if the 512-byte block is all zeros (the end-of-archive marker).
        bool is_zero_block(const u8* p) {
            for (usize i = 0; i < block_size; ++i) {
                if (p[i] != 0) {
                    return false;
                }
            }
            return true;
        }

        bool has_magic(const u8* p, const char* magic, usize length) {
            for (usize i = 0; i < length; ++i)
                if (p[i] != static_cast<u8>(magic[i]))
                    return false;
            return true;
        }

        bool parse_hex(const u8* p, u32& out) {
            u32 value = 0;
            for (usize i = 0; i < 8; ++i) {
                const u8 c = p[i];
                u32       digit;
                if (c >= '0' && c <= '9')
                    digit = c - '0';
                else if (c >= 'a' && c <= 'f')
                    digit = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F')
                    digit = c - 'A' + 10;
                else
                    return false;
                if (value > (~u32{0} >> 4))
                    return false;
                value = (value << 4) | digit;
            }
            out = value;
            return true;
        }

        bool cpio_checksum(const u8* data, usize size, u32 expected) {
            u32 sum = 0;
            for (usize i = 0; i < size; ++i)
                sum += data[i];
            return sum == expected;
        }

        // Read the header at `off`, requiring it to be a ustar header (not the
        // end marker) that lies wholly inside the archive.
        bool read_header(const archive& a, usize off, const u8*& hdr) {
            if (off > a.image.size() || block_size > a.image.size() - off) {
                return false;
            }
            const u8* p = a.image.data() + off;
            if (is_zero_block(p)) {
                return false; // end of archive
            }
            if (!has_ustar_magic(p + off_magic)) {
                return false;
            }
            // The name field must be a terminated string so callers can treat
            // it as one.
            if (!has_nul(p + off_name, name_len)) {
                return false;
            }
            hdr = p;
            return true;
        }

        bool read_cpio_entry(const archive& a, usize off, cpio_entry& out) {
            if (off > a.image.size() || cpio_header_size > a.image.size() - off)
                return false;
            const u8* hdr = a.image.data() + off;
            const bool crc = a.format == archive_format::cpio_crc;
            if (!has_magic(hdr, crc ? cpio_crc_magic : cpio_newc_magic, 6))
                return false;

            u32 ino = 0;
            u32 uid = 0;
            u32 gid = 0;
            u32 nlink = 0;
            u32 mtime = 0;
            u32 filesize = 0;
            u32 devmajor = 0;
            u32 devminor = 0;
            u32 rdevmajor = 0;
            u32 rdevminor = 0;
            u32 namesize = 0;
            if (!parse_hex(hdr + 6, ino) || !parse_hex(hdr + 14, out.mode) ||
                !parse_hex(hdr + 22, uid) || !parse_hex(hdr + 30, gid) ||
                !parse_hex(hdr + 38, nlink) || !parse_hex(hdr + 46, mtime) ||
                !parse_hex(hdr + 54, filesize) || !parse_hex(hdr + 62, devmajor) ||
                !parse_hex(hdr + 70, devminor) || !parse_hex(hdr + 78, rdevmajor) ||
                !parse_hex(hdr + 86, rdevminor) || !parse_hex(hdr + 94, namesize) ||
                !parse_hex(hdr + 102, out.check)) {
                return false;
            }
            (void) ino;
            (void) uid;
            (void) gid;
            (void) nlink;
            (void) mtime;
            (void) devmajor;
            (void) devminor;
            (void) rdevmajor;
            (void) rdevminor;

            if (namesize == 0)
                return false;
            const usize name_off = off + cpio_header_size;
            if (static_cast<usize>(namesize) > a.image.size() - name_off)
                return false;
            const usize name_size = static_cast<usize>(namesize);
            if (a.image.data()[name_off + name_size - 1] != 0)
                return false;
            const usize name_end = name_off + name_size;
            if (name_end > ~usize{0} - 3)
                return false;
            const usize data_off = (name_end + 3) & ~usize(3);
            if (data_off > a.image.size() ||
                static_cast<usize>(filesize) > a.image.size() - data_off)
                return false;
            const usize data_size = static_cast<usize>(filesize);
            const usize data_end = data_off + data_size;
            if (data_end > ~usize{0} - 3)
                return false;
            const usize next = (data_end + 3) & ~usize(3);
            if (a.format == archive_format::cpio_crc &&
                !cpio_checksum(a.image.data() + data_off, data_size, out.check))
                return false;

            out.name = a.image.data() + name_off;
            out.data = a.image.data() + data_off;
            out.size = data_size;
            out.next = next;
            return true;
        }

        bool is_cpio_trailer(const cpio_entry& entry) {
            return entry.size == 0 && str_eq(reinterpret_cast<const char*>(entry.name),
                                             cpio_trailer);
        }

    } // namespace

    archive_container detect_container(span<const u8> image) {
        if (image.data() == nullptr || image.size() == 0)
            return archive_container::raw;
        const u8* p = image.data();
        if (image.size() >= 2 && p[0] == gzip_magic_0 && p[1] == gzip_magic_1)
            return archive_container::gzip;
        if (image.size() >= 4 && p[0] == zstd_magic_0 && p[1] == zstd_magic_1 &&
            p[2] == zstd_magic_2 && p[3] == zstd_magic_3)
            return archive_container::zstd;
        return archive_container::raw;
    }

    bool open(span<const u8> image, archive& out) {
        if (image.data() == nullptr || image.size() < 6) {
            return false;
        }
        if (detect_container(image) != archive_container::raw)
            return false;
        archive a;
        a.image = image;
        a.base  = image.data();
        a.len   = image.size();

        if (has_magic(image.data(), cpio_newc_magic, 6) ||
            has_magic(image.data(), cpio_crc_magic, 6)) {
            a.format = has_magic(image.data(), cpio_crc_magic, 6)
                           ? archive_format::cpio_crc
                           : archive_format::cpio_newc;
            cpio_entry first;
            if (!read_cpio_entry(a, 0, first))
                return false;
        }
        else {
            if (image.size() < block_size)
                return false;
            // A valid archive begins with either a ustar header or, for an
            // empty one, the end-of-archive marker. Anything else is not tar.
            if (!is_zero_block(a.image.data())) {
                const u8* hdr = nullptr;
                if (!read_header(a, 0, hdr)) {
                    return false;
                }
            }
        }
        out = a;
        return true;
    }

    namespace {

        bool each_ustar(const archive& a, visit_fn visit, void* ctx) {
        if (a.image.data() == nullptr || a.image.size() < block_size) {
            return false;
        }
        usize off = 0;
        while (off < a.image.size()) {
            // A zero block here is the clean end-of-archive marker; anything
            // else that fails to read is a truncated or corrupt header.
            if (block_size > a.image.size() - off)
                return false;
            if (is_zero_block(a.image.data() + off)) {
                return true;
            }
            const u8* hdr = nullptr;
            if (!read_header(a, off, hdr)) {
                return false;
            }

            u64 size = 0;
            if (!parse_octal(reinterpret_cast<const char*>(hdr + off_size), size_len,
                             size)) {
                return false;
            }
            char type = static_cast<char>(hdr[off_typeflag]);

            usize data_off = off + block_size;
            if (size > a.image.size() || data_off > a.image.size() ||
                size > a.image.size() - data_off) {
                return false; // file data runs past the end of the archive
            }

            bool regular = (type == type_regular || type == type_regular_old);
            if (regular && size > 0 && visit != nullptr) {
                file f;
                f.name = reinterpret_cast<const char*>(hdr + off_name);
                f.data = hdr + block_size;
                f.size = static_cast<usize>(size);
                if (!visit(ctx, f)) {
                    return false; // caller asked to stop
                }
            }

            // Advance past the data rounded up to a whole block. If that lands
            // at or past the end, this was the last entry.
            u64 rounded = (size + (block_size - 1)) & ~static_cast<u64>(block_size - 1);
            u64 next    = data_off + rounded;
            if (next >= a.image.size()) {
                return true;
            }
            off = static_cast<usize>(next);
        }
        return true;
    }

        bool each_cpio(const archive& a, visit_fn visit, void* ctx) {
            if (a.image.data() == nullptr || a.image.size() < cpio_header_size)
                return false;
            usize off = 0;
            while (off < a.image.size()) {
                cpio_entry entry;
                if (!read_cpio_entry(a, off, entry))
                    return false;
                if (is_cpio_trailer(entry))
                    return true;
                if ((entry.mode & cpio_type_mask) == cpio_type_regular &&
                    entry.size > 0 && visit != nullptr) {
                    file f;
                    f.name = reinterpret_cast<const char*>(entry.name);
                    f.data = entry.data;
                    f.size = entry.size;
                    if (!visit(ctx, f))
                        return false;
                }
                if (entry.next <= off || entry.next > a.image.size())
                    return false;
                off = entry.next;
            }
            return false;
        }

    } // namespace

    bool each(const archive& a, visit_fn visit, void* ctx) {
        if (a.format == archive_format::ustar)
            return each_ustar(a, visit, ctx);
        return each_cpio(a, visit, ctx);
    }

    namespace {

        struct find_ctx {
            const char* name;
            file*       out;
            bool        found;
        };

        bool find_visit(void* ctx, const file& f) {
            auto* c = static_cast<find_ctx*>(ctx);
            if (str_eq(f.name, c->name)) {
                *c->out  = f;
                c->found = true;
                return false; // stop at the first match
            }
            return true;
        }

        struct count_ctx {
            usize n = 0;
        };

        bool count_visit(void* ctx, const file&) {
            ++static_cast<count_ctx*>(ctx)->n;
            return true;
        }

    } // namespace

    bool find(const archive& a, const char* name, file& out) {
        if (name == nullptr) {
            return false;
        }
        find_ctx c{name, &out, false};
        each(a, find_visit, &c);
        return c.found;
    }

    usize count(const archive& a) {
        count_ctx c;
        each(a, count_visit, &c);
        return c.n;
    }

} // namespace astia::initrd
