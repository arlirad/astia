// ustar tar reader.
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

        // Read the header at `off`, requiring it to be a ustar header (not the
        // end marker) that lies wholly inside the archive.
        bool read_header(const archive& a, usize off, const u8*& hdr) {
            if (off > a.len || block_size > a.len - off) {
                return false;
            }
            const u8* p = a.base + off;
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

    } // namespace

    bool open(span<const u8> image, archive& out) {
        if (image.data() == nullptr || image.size() < block_size) {
            return false;
        }
        archive a;
        a.base = image.data();
        a.len  = image.size();

        // A valid archive begins with either a ustar header or, for an empty
        // one, the end-of-archive marker. Anything else is not a tar.
        if (!is_zero_block(a.base)) {
            const u8* hdr = nullptr;
            if (!read_header(a, 0, hdr)) {
                return false;
            }
        }
        out = a;
        return true;
    }

    bool each(const archive& a, visit_fn visit, void* ctx) {
        if (a.base == nullptr || a.len < block_size) {
            return false;
        }
        usize off = 0;
        while (off < a.len) {
            // A zero block here is the clean end-of-archive marker; anything
            // else that fails to read is a truncated or corrupt header.
            if (is_zero_block(a.base + off)) {
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
            if (size > a.len || data_off > a.len || size > a.len - data_off) {
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
            if (next >= a.len) {
                return true;
            }
            off = static_cast<usize>(next);
        }
        return true;
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
