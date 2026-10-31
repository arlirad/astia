#pragma once

#include <astia/fs.hpp>
#include <astia/mapping.hpp>

namespace astia {
    template <>
    struct mapper<fs::file_permissions> {
        static optional<fs::file_permissions> from(i64 raw) {
            if (raw < 0 || raw > fs::file_permissions_mask)
                return optional<fs::file_permissions>(error_t::einval);
            return optional<fs::file_permissions>(
                fs::file_permissions(static_cast<u16>(raw)));
        }
    };

    template <>
    struct mapper<fs::file_mode> {
        static optional<fs::file_mode> from(i64 raw) {
            if (raw < 0 || raw > 0xffff)
                return optional<fs::file_mode>(error_t::einval);
            const u32     value = static_cast<u32>(raw);
            fs::file_type type  = fs::file_type::regular;
            switch (value & 0170000u) {
            case 0010000u:
                type = fs::file_type::fifo;
                break;
            case 0020000u:
                type = fs::file_type::character;
                break;
            case 0040000u:
                type = fs::file_type::directory;
                break;
            case 0100000u:
                type = fs::file_type::regular;
                break;
            case 0120000u:
                type = fs::file_type::symlink;
                break;
            case 0:
                break; // creation/chmod mode: regular file permissions
            default:
                return optional<fs::file_mode>(error_t::einval);
            }
            return optional<fs::file_mode>(
                fs::file_mode(type, fs::file_permissions(
                                         value & fs::file_permissions_mask)));
        }

        static u32 to(fs::file_mode mode) {
            u32 type = 0;
            switch (mode.type()) {
            case fs::file_type::fifo:
                type = 0010000u;
                break;
            case fs::file_type::character:
                type = 0020000u;
                break;
            case fs::file_type::directory:
                type = 0040000u;
                break;
            case fs::file_type::regular:
                type = 0100000u;
                break;
            case fs::file_type::symlink:
                type = 0120000u;
                break;
            case fs::file_type::socket:
                type = 0140000u;
                break;
            default:
                break;
            }
            return type | mode.permissions().raw();
        }
    };
}
