#include "boot.hpp"

#include <astia/mm/frame_allocator.hpp>

namespace astia::initrd {
    namespace {
        struct boot_state {
            bool              attempted = false;
            bool              ready     = false;
            bool              owned     = false;
            u64               physical  = 0;
            usz               frames    = 0;
            archive_container container = archive_container::raw;
            decode_error      error     = decode_error::ok;
            archive           value{};
            usz               source_size = 0;
            usz               stored_size = 0;
        };

        boot_state g_state{};

        boot_state& state() {
            return g_state;
        }

        bool publish_raw(boot_state& boot, span<const u8> image) {
            archive value;

            if (!open(image, value) || !validate(value)) {
                boot.error = decode_error::malformed;

                return false;
            }

            boot.value       = value;
            boot.stored_size = image.size();
            boot.ready       = true;

            return true;
        }

        bool publish_compressed(boot_state& boot, span<const u8> image) {
            decode_limits limits{};

            usz size = 0;

            boot.error = decoded_size(boot.container, image, limits, size);

            if (boot.error != decode_error::ok)
                return false;

            if (size == 0) {
                boot.error = decode_error::malformed;

                return false;
            }

            constexpr usz frame_size = mm::frame_allocator::frame_size;
            const usz     frames     = (size + frame_size - 1) / frame_size;
            const u64     physical =
                mm::kernel_frames().alloc(frames, mm::frame_flags::contiguous);

            if (physical == mm::frame_allocator::invalid_frame) {
                boot.error = decode_error::allocation;

                return false;
            }

            auto* const storage = static_cast<u8*>(mm::kernel_frames().to_virt(physical));

            boot.error = decode(boot.container, image, span<u8>(storage, size), limits);

            if (boot.error != decode_error::ok) {
                mm::kernel_frames().release(physical, frames);

                return false;
            }

            archive value;

            if (!open(span<const u8>(storage, size), value) || !validate(value)) {
                mm::kernel_frames().release(physical, frames);

                boot.error = decode_error::malformed;

                return false;
            }

            boot.physical    = physical;
            boot.frames      = frames;
            boot.owned       = true;
            boot.value       = value;
            boot.stored_size = size;
            boot.ready       = true;

            return true;
        }
    } // namespace

    bool load_boot_archive(const void* image, usz length) {
        boot_state& boot = state();

        if (boot.attempted)
            return boot.ready;

        boot.attempted = true;

        if (image == nullptr || length == 0) {
            boot.error = decode_error::malformed;

            return false;
        }

        const span<const u8> bytes(static_cast<const u8*>(image), length);

        boot.container   = detect_container(bytes);
        boot.source_size = length;

        if (boot.container == archive_container::raw)
            return publish_raw(boot, bytes);

        return publish_compressed(boot, bytes);
    }

    bool boot_archive_ready() {
        return state().ready;
    }

    bool open_boot_archive(archive& out) {
        const boot_state& boot = state();

        if (!boot.ready)
            return false;

        out = boot.value;

        return true;
    }

    archive_container boot_archive_container() {
        return state().container;
    }

    usz boot_archive_source_size() {
        return state().source_size;
    }

    usz boot_archive_stored_size() {
        return state().stored_size;
    }

    decode_error boot_archive_error() {
        return state().error;
    }
} // namespace astia::initrd
