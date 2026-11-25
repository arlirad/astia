#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/error.hpp>
#include <astia/fs.hpp>
#include <astia/mm/frame_allocator.hpp>

#include "../io/aio/dispatch.hpp"
#include "module.hpp"

namespace astia::mod {

    namespace {

        /// Contiguous frame allocation that releases itself when the
        /// coroutine frame is destroyed, including a canceled read.
        class image_buffer {
            public:
            image_buffer()                               = default;
            image_buffer(const image_buffer&)            = delete;
            image_buffer& operator=(const image_buffer&) = delete;

            ~image_buffer() {
                release();
            }

            bool allocate(usize bytes) {
                release();
                if (bytes == 0) {
                    return false;
                }
                const usize frames = (bytes + mm::frame_allocator::frame_size - 1) /
                                     mm::frame_allocator::frame_size;
                const u64 physical =
                    mm::kernel_frames().alloc(frames, mm::frame_flags::contiguous);
                if (physical == mm::frame_allocator::invalid_frame) {
                    return false;
                }
                _physical = physical;
                _frames   = frames;
                _bytes    = bytes;
                return true;
            }

            span<u8> view() const {
                auto* address = static_cast<u8*>(mm::kernel_frames().to_virt(_physical));
                return span<u8>(address, _bytes);
            }

            private:
            void release() {
                if (_physical != mm::frame_allocator::invalid_frame) {
                    mm::kernel_frames().release(_physical, _frames);
                }
                _physical = mm::frame_allocator::invalid_frame;
                _frames   = 0;
                _bytes    = 0;
            }

            u64   _physical = mm::frame_allocator::invalid_frame;
            usize _frames   = 0;
            usize _bytes    = 0;
        };

        module_error path_error(long result) {
            switch (-result) {
            case ENOENT:
            case ENOTDIR:
                return module_error::not_found;
            case EACCES:
                return module_error::access;
            case ENAMETOOLONG:
            case EINVAL:
                return module_error::invalid_argument;
            default:
                return module_error::invalid_argument;
            }
        }

    } // namespace

    core::task<module_open_result> open_path(proc::process& caller, const char* path,
                                             const module_load_options& options) {
        module_open_result result{};
        if (path == nullptr || path[0] == '\0') {
            result.error = module_error::invalid_argument;
            co_return result;
        }

        char       resolved[fs::path_max]{};
        const long composed =
            io::aio::dispatch::resolve_at(&caller, -1, path, resolved, sizeof resolved);
        if (composed < 0) {
            result.error = path_error(composed);
            co_return result;
        }
        const long canonical =
            fs::resolve_path(resolved, resolved, sizeof resolved, true);
        if (canonical < 0) {
            result.error = path_error(canonical);
            co_return result;
        }

        const fs::file_mode mode = fs::node_mode(resolved);
        if (!mode.exists()) {
            result.error = module_error::not_found;
            co_return result;
        }
        if (!mode.is_regular()) {
            result.error = module_error::load_failed;
            co_return result;
        }
        if (!caller.can_access(mode, fs::node_uid(resolved), fs::node_gid(resolved),
                               fs::permission_bits::read)) {
            result.error = module_error::access;
            co_return result;
        }

        auto file = fs::open_path(resolved, ASTIA_AIO_O_RDONLY);
        if (!file) {
            result.error = module_error::not_found;
            co_return result;
        }
        const long size = file->size();
        if (size <= 0) {
            result.error = module_error::load_failed;
            co_return result;
        }
        const usize bytes = static_cast<usize>(size);
        if (bytes > max_module_image_bytes) {
            result.error = module_error::limit;
            co_return result;
        }

        image_buffer image;
        if (!image.allocate(bytes)) {
            result.error = module_error::no_space;
            co_return result;
        }
        const auto read = co_await file->read_exact(image.view(), 0, {});
        if (!read) {
            result.error = read.error == error_t::enomem ? module_error::no_space
                                                         : module_error::load_failed;
            co_return result;
        }

        result.error =
            validate_module_image_format(span<const u8>(image.view().data(), bytes));
        if (result.error != module_error::ok) {
            co_return result;
        }

        const auto loaded =
            co_await open(span<const u8>(image.view().data(), bytes), options);
        if (!loaded) {
            result.error = loaded.error == error_t::enomem ? module_error::no_space
                                                           : module_error::load_failed;
            co_return result;
        }
        result = loaded.value;
        co_return result;
    }

} // namespace astia::mod
