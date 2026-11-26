// Uncompressed newc/cpio-crc reader.

#include "initrd.hpp"

#include <astia/strings.hpp>

namespace astia::initrd {
    namespace {
        constexpr usize cpio_header_size  = 110;
        constexpr char  cpio_newc_magic[] = "070701";
        constexpr char  cpio_crc_magic[]  = "070702";
        constexpr u8    gzip_magic_0      = 0x1f;
        constexpr u8    gzip_magic_1      = 0x8b;
        constexpr u8    zstd_magic_0      = 0x28;
        constexpr u8    zstd_magic_1      = 0xb5;
        constexpr u8    zstd_magic_2      = 0x2f;
        constexpr u8    zstd_magic_3      = 0xfd;
        constexpr u8    zstd_skippable_0  = 0x50;
        constexpr u8    zstd_skippable_1  = 0x2a;
        constexpr u8    zstd_skippable_2  = 0x4d;
        constexpr u8    zstd_skippable_3  = 0x18;
        constexpr u32   cpio_type_mask    = 0170000;
        constexpr u32   cpio_type_regular = 0100000;
        constexpr char  cpio_trailer[]    = "TRAILER!!!";

        struct cpio_entry {
            const u8* name      = nullptr;
            const u8* data      = nullptr;
            usize     name_size = 0;
            usize     size      = 0;
            u32       mode      = 0;
            u32       check     = 0;
            usize     next      = 0;
        };

        bool has_magic(const u8* p, const char* magic, usize length) {
            for (usize i = 0; i < length; ++i) {
                if (p[i] != static_cast<u8>(magic[i]))
                    return false;
            }

            return true;
        }

        bool parse_hex(const u8* p, u32& out) {
            u32 value = 0;

            for (usize i = 0; i < 8; ++i) {
                const u8 c = p[i];
                u32      digit;

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

            for (usize i = 0; i < size; ++i) {
                sum += data[i];
            }

            return sum == expected;
        }

        bool read_cpio_entry(const archive& a, usize off, cpio_entry& out) {
            if (off > a.image.size() || cpio_header_size > a.image.size() - off)
                return false;

            const u8* hdr = a.image.data() + off;

            const bool crc = a.format == archive_format::cpio_crc;

            if (!has_magic(hdr, crc ? cpio_crc_magic : cpio_newc_magic, 6))
                return false;

            u32 ino       = 0;
            u32 uid       = 0;
            u32 gid       = 0;
            u32 nlink     = 0;
            u32 mtime     = 0;
            u32 filesize  = 0;
            u32 devmajor  = 0;
            u32 devminor  = 0;
            u32 rdevmajor = 0;
            u32 rdevminor = 0;
            u32 namesize  = 0;

            if (!parse_hex(hdr + 6, ino) || !parse_hex(hdr + 14, out.mode) ||
                !parse_hex(hdr + 22, uid) || !parse_hex(hdr + 30, gid) ||
                !parse_hex(hdr + 38, nlink) || !parse_hex(hdr + 46, mtime) ||
                !parse_hex(hdr + 54, filesize) || !parse_hex(hdr + 62, devmajor) ||
                !parse_hex(hdr + 70, devminor) || !parse_hex(hdr + 78, rdevmajor) ||
                !parse_hex(hdr + 86, rdevminor) || !parse_hex(hdr + 94, namesize) ||
                !parse_hex(hdr + 102, out.check))
                return false;

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

            string_view bounded_name;

            if (!string_view{reinterpret_cast<const char*>(a.image.data() + name_off),
                             name_size}
                     .find_nul(bounded_name) ||
                bounded_name.size != name_size - 1)
                return false;

            const usize name_end = name_off + name_size;

            if (name_end > ~usize{0} - 3)
                return false;

            const usize data_off = (name_end + 3) & ~usize(3);

            if (data_off > a.image.size() ||
                static_cast<usize>(filesize) > a.image.size() - data_off)
                return false;

            const usize data_size = static_cast<usize>(filesize);
            const usize data_end  = data_off + data_size;

            if (data_end > ~usize{0} - 3)
                return false;

            const usize next = (data_end + 3) & ~usize(3);

            if (a.format == archive_format::cpio_crc &&
                !cpio_checksum(a.image.data() + data_off, data_size, out.check))
                return false;

            out.name      = a.image.data() + name_off;
            out.data      = a.image.data() + data_off;
            out.name_size = bounded_name.size;
            out.size      = data_size;
            out.next      = next;

            return true;
        }

        bool is_cpio_trailer(const cpio_entry& entry) {
            return entry.size == 0 &&
                   string_view{reinterpret_cast<const char*>(entry.name), entry.name_size}
                       .equals({cpio_trailer, sizeof(cpio_trailer) - 1});
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

        if (image.size() >= 4 && p[0] >= zstd_skippable_0 && p[0] <= 0x5f &&
            p[1] == zstd_skippable_1 && p[2] == zstd_skippable_2 &&
            p[3] == zstd_skippable_3)
            return archive_container::zstd_skippable;

        return archive_container::raw;
    }

    bool open(span<const u8> image, archive& out) {
        if (image.data() == nullptr || image.size() < 6)
            return false;

        if (detect_container(image) != archive_container::raw)
            return false;

        archive a;

        a.image = image;
        a.base  = image.data();
        a.len   = image.size();

        if (!has_magic(image.data(), cpio_newc_magic, 6) &&
            !has_magic(image.data(), cpio_crc_magic, 6))
            return false;

        a.format = has_magic(image.data(), cpio_crc_magic, 6) ? archive_format::cpio_crc
                                                              : archive_format::cpio_newc;

        cpio_entry first;

        if (!read_cpio_entry(a, 0, first))
            return false;

        out = a;

        return true;
    }

    namespace {
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
        return each_cpio(a, visit, ctx);
    }

    bool validate(const archive& a) {
        return each_cpio(a, nullptr, nullptr);
    }

    namespace {
        struct find_ctx {
            const char* name;
            file*       out;
            bool        found;
        };

        bool find_visit(void* ctx, const file& f) {
            auto* c = static_cast<find_ctx*>(ctx);

            if (string_view::from_c_str(f.name).equals(
                    string_view::from_c_str(c->name))) {
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
        if (name == nullptr)
            return false;

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
