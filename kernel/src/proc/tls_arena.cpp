#include "tls_arena.hpp"

#include <astia/proc/user_layout.hpp>

namespace astia::proc {
    namespace {
        constexpr usz tls_reserve_pages =
            user_dso_tls_reserve / mm::phys_region::frame_size;
        static_assert(user_dso_tls_reserve % mm::phys_region::frame_size == 0,
                      "the DSO TLS reserve must be a whole number of pages");
        static_assert(tls_reserve_pages != 0);
    } // namespace

    bool measure_tls_arena(usz payload_pages, tls_arena& out) {
        if (payload_pages == 0 || payload_pages > ~usz{0} - tls_reserve_pages)
            return false;

        out.reserve_bytes  = user_dso_tls_reserve;
        out.reserve_pages  = tls_reserve_pages;
        out.payload_pages  = payload_pages;
        out.total_pages    = tls_reserve_pages + payload_pages;
        out.payload_offset = user_dso_tls_reserve;

        return true;
    }

    mm::phys_region* create_tls_arena(const tls_arena& layout) {
        if (layout.payload_pages == 0 || layout.reserve_pages != tls_reserve_pages ||
            layout.total_pages < layout.reserve_pages ||
            layout.total_pages - layout.reserve_pages != layout.payload_pages)
            return nullptr;

        auto* region = mm::phys_region::create_lazy(layout.total_pages);
        if (region == nullptr)
            return nullptr;

        for (usz page = layout.reserve_pages; page < layout.total_pages; ++page) {
            if (!region->ensure_page(page, true)) {
                region->release();

                return nullptr;
            }
        }

        return region;
    }

    bool write_tls_payload(mm::phys_region& region, const tls_arena& layout, u64 offset,
                           span<const u8> source) {
        usz done = 0;

        while (done < source.size()) {
            const u64 within = layout.payload_offset + offset + done;
            const usz page   = static_cast<usz>(within / mm::phys_region::frame_size);
            const usz page_offset =
                static_cast<usz>(within % mm::phys_region::frame_size);

            if (page >= layout.total_pages)
                return false;

            auto* target = static_cast<u8*>(region.page_direct(page));
            if (target == nullptr)
                return false;

            usz chunk = mm::phys_region::frame_size - page_offset;

            if (chunk > source.size() - done)
                chunk = source.size() - done;

            __builtin_memcpy(target + page_offset, source.data() + done, chunk);

            done += chunk;
        }

        return true;
    }

    bool write_tls_word(mm::phys_region& region, const tls_arena& layout, u64 offset,
                        u64 value) {
        return write_tls_payload(
            region, layout, offset,
            span<const u8>(reinterpret_cast<const u8*>(&value), sizeof(value)));
    }
} // namespace astia::proc
