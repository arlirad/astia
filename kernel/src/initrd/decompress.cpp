// Bounded gzip/zstd outer-container decoding.

#define ZSTD_STATIC_LINKING_ONLY

#include "decompress.hpp"

#include <miniz_tinfl.h>
#include <zstd_errors.h>

#include <zstd.h>

namespace astia::initrd {
    namespace {
        constexpr usz gzip_header_min   = 10;
        constexpr usz gzip_trailer_size = 8;

        constexpr u8 gzip_flag_header_crc = 0x02;
        constexpr u8 gzip_flag_extra      = 0x04;
        constexpr u8 gzip_flag_name       = 0x08;
        constexpr u8 gzip_flag_comment    = 0x10;
        constexpr u8 gzip_flag_reserved   = 0xe0;

        constexpr usz zstd_workspace_bytes = 128 * 1024;

        // The kernel is single-threaded during init, which is the only caller
        // of this decoder.  Both decoder states live in BSS so a malformed
        // archive cannot exhaust the 16 KiB boot stack.
        tinfl_decompressor inflate_state;
        alignas(64) u8 zstd_workspace[zstd_workspace_bytes];

        u32 crc32_update(u32 state, const u8* data, usz size) {
            for (usz i = 0; i < size; ++i) {
                state ^= data[i];

                for (u32 bit = 0; bit < 8; ++bit) {
                    const u32 mask = 0u - (state & 1u);

                    state = (state >> 1) ^ (0xedb88320u & mask);
                }
            }

            return state;
        }

        u16 read_le16(const u8* p) {
            return static_cast<u16>(static_cast<u16>(p[0]) |
                                    static_cast<u16>(static_cast<u16>(p[1]) << 8));
        }

        u32 read_le32(const u8* p) {
            return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
                   (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
        }

        bool range_ok(usz offset, usz length, usz limit) {
            return offset <= limit && length <= limit - offset;
        }

        decode_error check_sizes(usz compressed, u64 declared,
                                 const decode_limits& limits, usz& size) {
            if (declared > limits.max_output)
                return decode_error::output_limit;

            size = static_cast<usz>(declared);

            if (size <= limits.ratio_floor)
                return decode_error::ok;

            if (compressed == 0 || limits.max_ratio == 0)
                return decode_error::ratio_limit;

            const usz product = compressed > (~usz{0}) / limits.max_ratio
                                    ? ~usz{0}
                                    : compressed * limits.max_ratio;

            if (size > product)
                return decode_error::ratio_limit;

            return decode_error::ok;
        }

        struct gzip_view {
            usz deflate_offset    = 0;
            usz deflate_size      = 0;
            u32 crc               = 0;
            u32 uncompressed_size = 0;
        };

        decode_error parse_gzip(span<const u8> image, gzip_view& view) {
            if (image.data() == nullptr ||
                image.size() < gzip_header_min + gzip_trailer_size)
                return decode_error::truncated;

            if (image[0] != 0x1f || image[1] != 0x8b || image[2] != 8)
                return decode_error::malformed;

            const u8 flags = image[3];

            if ((flags & gzip_flag_reserved) != 0)
                return decode_error::unsupported;

            const usz trailer = image.size() - gzip_trailer_size;
            usz       offset  = gzip_header_min;
            u32       crc     = 0xffffffffu;

            crc = crc32_update(crc, image.data(), gzip_header_min);

            if ((flags & gzip_flag_extra) != 0) {
                if (!range_ok(offset, 2, trailer))
                    return decode_error::truncated;

                const usz extra_size = read_le16(image.data() + offset);
                const usz start      = offset;

                offset += 2;

                if (!range_ok(offset, extra_size, trailer))
                    return decode_error::truncated;

                offset += extra_size;
                crc = crc32_update(crc, image.data() + start, offset - start);
            }

            if ((flags & gzip_flag_name) != 0) {
                const usz start = offset;

                while (offset < trailer && image[offset] != 0) {
                    ++offset;
                }

                if (offset == trailer)
                    return decode_error::truncated;

                ++offset;

                crc = crc32_update(crc, image.data() + start, offset - start);
            }

            if ((flags & gzip_flag_comment) != 0) {
                const usz start = offset;

                while (offset < trailer && image[offset] != 0) {
                    ++offset;
                }

                if (offset == trailer)
                    return decode_error::truncated;

                ++offset;

                crc = crc32_update(crc, image.data() + start, offset - start);
            }

            if ((flags & gzip_flag_header_crc) != 0) {
                if (!range_ok(offset, 2, trailer))
                    return decode_error::truncated;

                const u16 expected = static_cast<u16>(~crc);

                if (read_le16(image.data() + offset) != expected)
                    return decode_error::checksum;

                offset += 2;
            }

            if (offset >= trailer)
                return decode_error::malformed;

            view.deflate_offset    = offset;
            view.deflate_size      = trailer - offset;
            view.crc               = read_le32(image.data() + trailer);
            view.uncompressed_size = read_le32(image.data() + trailer + 4);

            return decode_error::ok;
        }

        decode_error map_zstd_error(usz code) {
            switch (ZSTD_getErrorCode(code)) {
            case ZSTD_error_checksum_wrong:
                return decode_error::checksum;
            case ZSTD_error_memory_allocation:
                return decode_error::allocation;
            case ZSTD_error_frameParameter_unsupported:
                return decode_error::unsupported;
            case ZSTD_error_frameParameter_windowTooLarge:
                return decode_error::window_limit;
            case ZSTD_error_srcSize_wrong:
                return decode_error::truncated;
            default:
                return decode_error::malformed;
            }
        }
    } // namespace

    extern "C" {
    void* astia_zstd_malloc(size_t size) {
        (void) size;

        return nullptr;
    }

    void* astia_zstd_calloc(size_t count, size_t size) {
        (void) count;
        (void) size;

        return nullptr;
    }

    void astia_zstd_free(void* pointer) {
        (void) pointer;
    }
    }

    const char* decode_error_name(decode_error error) {
        switch (error) {
        case decode_error::ok:
            return "ok";
        case decode_error::unsupported:
            return "unsupported";
        case decode_error::malformed:
            return "malformed";
        case decode_error::truncated:
            return "truncated";
        case decode_error::checksum:
            return "checksum mismatch";
        case decode_error::input_limit:
            return "compressed input limit";
        case decode_error::output_limit:
            return "decompressed output limit";
        case decode_error::ratio_limit:
            return "expansion ratio limit";
        case decode_error::window_limit:
            return "decoder window limit";
        case decode_error::trailing_data:
            return "trailing data";
        case decode_error::allocation:
            return "decoder allocation";
        }

        return "unknown";
    }

    const char* container_name(archive_container container) {
        switch (container) {
        case archive_container::raw:
            return "raw";
        case archive_container::gzip:
            return "gzip";
        case archive_container::zstd:
            return "zstd";
        case archive_container::zstd_skippable:
            return "zstd-skippable";
        }

        return "unknown";
    }

    decode_error decoded_size(archive_container container, span<const u8> image,
                              const decode_limits& limits, usz& size) {
        if (image.data() == nullptr || image.size() == 0)
            return decode_error::malformed;

        if (image.size() > limits.max_input)
            return decode_error::input_limit;

        if (container == archive_container::gzip) {
            gzip_view          view;
            const decode_error error = parse_gzip(image, view);

            if (error != decode_error::ok)
                return error;

            return check_sizes(image.size(), view.uncompressed_size, limits, size);
        }

        if (container == archive_container::zstd) {
            ZSTD_FrameHeader header{};

            const usz result = ZSTD_getFrameHeader(&header, image.data(),
                                                   static_cast<usz>(image.size()));

            if (ZSTD_isError(result))
                return map_zstd_error(result);

            if (result != 0)
                return decode_error::truncated;

            if (header.frameType != ZSTD_frame)
                return decode_error::unsupported;

            if (header.frameContentSize == ZSTD_CONTENTSIZE_UNKNOWN)
                return decode_error::unsupported;

            if (header.frameContentSize == ZSTD_CONTENTSIZE_ERROR)
                return decode_error::malformed;

            if (header.windowSize > limits.max_window)
                return decode_error::window_limit;

            const usz frame_size = ZSTD_findFrameCompressedSize(
                image.data(), static_cast<usz>(image.size()));

            if (ZSTD_isError(frame_size))
                return map_zstd_error(frame_size);

            if (frame_size != image.size())
                return decode_error::trailing_data;

            return check_sizes(image.size(), header.frameContentSize, limits, size);
        }

        return decode_error::unsupported;
    }

    decode_error decode(archive_container container, span<const u8> image,
                        span<u8> output, const decode_limits& limits) {
        usz expected = 0;

        const decode_error size_error = decoded_size(container, image, limits, expected);

        if (size_error != decode_error::ok)
            return size_error;

        if (output.size() != expected || (output.size() != 0 && output.data() == nullptr))
            return decode_error::malformed;

        if (container == archive_container::gzip) {
            gzip_view    view;
            decode_error error = parse_gzip(image, view);

            if (error != decode_error::ok)
                return error;

            tinfl_init(&inflate_state);

            usz in_size  = view.deflate_size;
            usz out_size = output.size();

            const tinfl_status status =
                tinfl_decompress(&inflate_state, image.data() + view.deflate_offset,
                                 &in_size, output.data(), output.data(), &out_size,
                                 TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);

            if (status != TINFL_STATUS_DONE)
                return status == TINFL_STATUS_NEEDS_MORE_INPUT ? decode_error::truncated
                                                               : decode_error::malformed;

            if (in_size != view.deflate_size)
                return decode_error::trailing_data;

            if (out_size != output.size())
                return decode_error::malformed;

            if (crc32_update(0xffffffffu, output.data(), output.size()) != ~view.crc)
                return decode_error::checksum;

            if (static_cast<u32>(output.size()) != view.uncompressed_size)
                return decode_error::malformed;

            return decode_error::ok;
        }

        if (container == archive_container::zstd) {
            if (ZSTD_estimateDCtxSize() > zstd_workspace_bytes)
                return decode_error::allocation;

            ZSTD_DCtx* context =
                ZSTD_initStaticDCtx(zstd_workspace, zstd_workspace_bytes);
            if (context == nullptr)
                return decode_error::allocation;

            const usz result =
                ZSTD_decompressDCtx(context, output.data(), output.size(), image.data(),
                                    static_cast<usz>(image.size()));

            if (ZSTD_isError(result))
                return map_zstd_error(result);

            if (result != output.size())
                return decode_error::malformed;

            return decode_error::ok;
        }

        return decode_error::unsupported;
    }
} // namespace astia::initrd
