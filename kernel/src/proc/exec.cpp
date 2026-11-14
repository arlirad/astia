#include "proc.hpp"
#include "process.hpp"
#include <astia/api/syscalls.h>
#include <astia/arch/page_map.hpp>
#include <astia/elf.hpp>
#include <astia/fs.hpp>
#include <astia/mm/frame_allocator.hpp>

namespace astia::proc {

    namespace {
        /// One PT_LOAD segment's physical run, recorded at map time so
        /// the load can file it as a VMA afterwards.
        struct mapped_segment {
            u64  virt   = 0;
            u64  frames = 0;
            u64  pages  = 0;
            bool used   = false;
        };

        struct image_map {
            arch::page_map* map;
            u64             next_base;
            mapped_segment  segs[elf::max_segments]{};
            u32             count = 0;
        };

        struct stream_context {
            image_map                map;
            mm::shptr<fs::open_file> file;

            ~stream_context();
        };

        void unmap_run(arch::page_map& map, u64 virt, u64 pages) {
            for (u64 page = 0; page < pages; ++page) {
                (void) map.unmap(virt + page * elf::page_size);
            }
        }

        void cleanup_pending(image_map& image, arch::page_map& map) {
            for (u32 i = 0; i < image.count; ++i) {
                auto& seg = image.segs[i];
                if (!seg.used) {
                    continue;
                }
                unmap_run(map, seg.virt, seg.pages);
                mm::kernel_frames().release(seg.frames, seg.pages);
                seg.used = false;
            }
        }

        stream_context::~stream_context() {
            if (map.map != nullptr)
                cleanup_pending(map, *map.map);
        }

        void* map_user(void* opaque, u64 hint, usize size, usize align, bool fixed) {
            auto&     image = *static_cast<image_map*>(opaque);
            const u64 base  = fixed ? hint : (image.next_base + align - 1) & ~(align - 1);
            if (base < user_image_base || base + size >= user_address_limit ||
                image.count >= elf::max_segments) {
                return nullptr;
            }

            // One contiguous frame run per segment: the VMA that files
            // this range adopts it as a single phys_region — the unit
            // fork's reference counting and CoW splits work in.
            const u64 pages  = (size + elf::page_size - 1) / elf::page_size;
            const u64 frames = mm::kernel_frames().alloc(pages, mm::frame_flags::zeroed);
            if (frames == mm::frame_allocator::invalid_frame) {
                return nullptr;
            }
            for (u64 page = 0; page < pages; ++page) {
                if (!image.map->map_4k(
                        base + page * elf::page_size, frames + page * elf::page_size,
                        arch::page_flags::present | arch::page_flags::writable |
                            arch::page_flags::user)) {
                    unmap_run(*image.map, base, page);
                    mm::kernel_frames().release(frames, pages);
                    return nullptr;
                }
            }
            image.segs[image.count] = mapped_segment{base, frames, pages, true};
            ++image.count;
            image.next_base = base + size + elf::page_size;
            return reinterpret_cast<void*>(base);
        }

        bool copy_image(void* opaque, u64 address, span<const u8> source) {
            auto& image = *static_cast<image_map*>(opaque);
            for (u32 i = 0; i < image.count; ++i) {
                const auto& seg = image.segs[i];
                if (address < seg.virt ||
                    address + source.size() > seg.virt + seg.pages * elf::page_size)
                    continue;
                auto* destination =
                    static_cast<u8*>(mm::kernel_frames().to_virt(seg.frames));
                __builtin_memcpy(destination + (address - seg.virt), source.data(),
                                 source.size());
                return true;
            }
            return false;
        }

        // The ELF loader reserves and maps one contiguous span for all
        // PT_LOAD segments.  Until VMAs can split that span at each
        // segment boundary, keep the span executable and writable:
        // this preserves the loader's established mapping contract and
        // lets fork/CoW account for it as one physical region.  The
        // page-granular permission split is a later mprotect/exec slice.
        map_protection segment_prot(const elf::image& image, u64 base) {
            (void) image;
            (void) base;
            return map_protection::read | map_protection::write | map_protection::exec;
        }

        // Where the static TLS block lives: a fixed base well below the
        // user stack (create_user_thread owns the stack near the top of
        // the user half).
        // Mapped headroom after the thread pointer: the first word is the
        // self-pointer used by Clang's x86_64 TLS sequence, and the rest is
        // available to the userspace TCB.

        // Materialize the PT_TLS template in the child: user pages, the
        // init bytes copied out of the file, the rest zeroed.  The
        // block's end, aligned up, is the x86_64 thread pointer.  The
        // frames come as one contiguous run so the block can be filed
        // as a single VMA (fork shares it copy-on-write); frames_out
        // hands the run back for that adoption.
        bool map_tls_template(arch::page_map& map, span<const u8> bytes,
                              const elf::image& image, u64& tp_out, u64& frames_out,
                              u64& pages_out) {
            const u64 align = image.tls_align == 0 ? 1 : image.tls_align;
            const u64 tls_span = (image.tls_memsz + align - 1) & ~(align - 1);
            const u64 pages =
                (tls_span + user_tls_tcb_headroom + elf::page_size - 1) / elf::page_size;
            if (image.tls_offset > bytes.size() ||
                image.tls_filesz > bytes.size() - image.tls_offset)
                return false;
            const auto source = astia::span<const u8>(
                bytes.data() + image.tls_offset, static_cast<usize>(image.tls_filesz));
            const u64 frames = mm::kernel_frames().alloc(pages, mm::frame_flags::zeroed);
            if (frames == mm::frame_allocator::invalid_frame) {
                return false;
            }
            for (u64 page = 0; page < pages; ++page) {
                if (!map.map_4k(user_tls_base + page * elf::page_size,
                                frames + page * elf::page_size,
                                arch::page_flags::present | arch::page_flags::writable |
                                    arch::page_flags::user)) {
                    unmap_run(map, user_tls_base, page);
                    mm::kernel_frames().release(frames, pages);
                    return false;
                }
            }
            auto* destination = static_cast<u8*>(mm::kernel_frames().to_virt(frames));
            for (u64 index = 0; index < source.size() && index < image.tls_memsz;
                 ++index) {
                destination[index] = source.data()[index];
            }
            tp_out                                          = user_tls_base + tls_span;
            *reinterpret_cast<u64*>(destination + tls_span) = tp_out;
            frames_out                                  = frames;
            pages_out                                   = pages;
            return true;
        }
    } // namespace

    bool process::load_user_image(span<const u8> bytes, u64* entry_out) {
        if (bytes.null() || bytes.empty() || entry_out == nullptr ||
            address_space_ != nullptr || arch::kernel_page_map() == nullptr) {
            return false;
        }

        arch::page_map map = arch::page_map::create(arch::kernel_page_map());
        if (!map.valid()) {
            return false;
        }
        image_map  image_state{&map, user_image_base};
        elf::env   environment{&image_state, map_user, nullptr, copy_image};
        elf::image image{};

        // The ELF implementation copies segment bytes through the virtual
        // addresses returned by map_fn, so the child map must be active while
        // it performs the load. Kernel-half mappings are shared by create().
        // Whichever map was active on the way in comes back on the way out:
        // spawnve may be running inside a ring-3 process's syscall, and
        // dropping to the kernel map there faults the caller's very next
        // user instruction.
        const process*        caller = current_process();
        const arch::page_map* caller_map =
            caller == nullptr ? nullptr : caller->address_space();
        map.activate();
        const bool loaded = elf::load_executable(bytes, environment, image);
        if (caller_map != nullptr) {
            caller_map->activate();
        }
        else {
            arch::kernel_page_map()->activate();
        }
        if (!loaded) {
            cleanup_pending(image_state, map);
            return false;
        }

        address_space_.reset(new arch::page_map(static_cast<arch::page_map&&>(map)));
        if (address_space_ == nullptr) {
            cleanup_pending(image_state, map);
            return false;
        }

        // File every segment as a VMA: the frames (mapped above)
        // transfer to a phys_region, so fork and copy-on-write
        // account for the image like any other mapping.
        for (u32 i = 0; i < image_state.count; ++i) {
            const auto& seg = image_state.segs[i];
            if (!seg.used) {
                continue;
            }
            auto* region = mm::phys_region::adopt(seg.frames, seg.pages);
            if (region == nullptr ||
                !register_mapping(seg.virt, seg.pages, segment_prot(image, seg.virt),
                                  region)) {
                if (region != nullptr) {
                    region->release();
                    image_state.segs[i].used = false;
                }
                cleanup_pending(image_state, *address_space_);
                discard_image();
                return false;
            }
            image_state.segs[i].used = false;
        }

        // Static TLS: the thread's %fs base loads the block end this
        // materializes (0 = the image has no PT_TLS and nothing ever
        // reaches %fs).
        if (image.tls_memsz != 0) {
            u64 tp         = 0;
            u64 tls_frames = 0;
            u64 tls_pages  = 0;
            if (!map_tls_template(*address_space_, bytes, image, tp, tls_frames,
                                  tls_pages)) {
                cleanup_pending(image_state, *address_space_);
                discard_image();
                return false;
            }
            tls_tp_      = tp;
            auto* region = mm::phys_region::adopt(tls_frames, tls_pages);
            if (region == nullptr ||
                !register_mapping(user_tls_base, tls_pages,
                                  map_protection::read | map_protection::write, region)) {
                if (region != nullptr) {
                    region->release();
                }
                else {
                    unmap_run(*address_space_, user_tls_base, tls_pages);
                    mm::kernel_frames().release(tls_frames, tls_pages);
                }
                if (region != nullptr)
                    unmap_run(*address_space_, user_tls_base, tls_pages);
                cleanup_pending(image_state, *address_space_);
                discard_image();
                return false;
            }
        }

        *entry_out = reinterpret_cast<u64>(image.entry);
        return true;
    }

    namespace {
        core::task<i64> read_stream_async(void* opaque, span<u8> destination, u64 offset) {
            auto* context = static_cast<stream_context*>(opaque);
            if (context == nullptr || !context->file)
                co_return error_t::ebadf;
            auto result = co_await fs::read_exact(context->file, destination, offset);
            if (!result)
                co_return result.error;
            co_return result.value;
        }

        void unmap_user_stream(void* opaque, void* address, usize size) {
            auto* context = static_cast<stream_context*>(opaque);
            if (context == nullptr || context->map.map == nullptr)
                return;
            for (u32 i = 0; i < context->map.count; ++i) {
                auto& segment = context->map.segs[i];
                if (segment.used && segment.virt == reinterpret_cast<u64>(address) &&
                    segment.pages * elf::page_size == size) {
                    unmap_run(*context->map.map, segment.virt, segment.pages);
                    mm::kernel_frames().release(segment.frames, segment.pages);
                    segment.used = false;
                    return;
                }
            }
        }

        void* map_user_stream(void* opaque, u64 hint, usize size, usize align,
                              bool fixed) {
            return map_user(&static_cast<stream_context*>(opaque)->map, hint, size, align,
                            fixed);
        }

        core::task<bool> map_tls_file_async(arch::page_map& map,
                                             const mm::shptr<fs::open_file>& file,
                                             const elf::image& image, u64& tp_out,
                                             u64& frames_out, u64& pages_out) {
            const u64 align = image.tls_align == 0 ? 1 : image.tls_align;
            const u64 tls_span = (image.tls_memsz + align - 1) & ~(align - 1);
            const u64 pages =
                (tls_span + user_tls_tcb_headroom + elf::page_size - 1) / elf::page_size;
            const u64 frames = mm::kernel_frames().alloc(pages, mm::frame_flags::zeroed);
            if (frames == mm::frame_allocator::invalid_frame)
                co_return false;
            for (u64 page = 0; page < pages; ++page) {
                if (!map.map_4k(user_tls_base + page * elf::page_size,
                                frames + page * elf::page_size,
                                arch::page_flags::present | arch::page_flags::writable |
                                    arch::page_flags::user)) {
                    unmap_run(map, user_tls_base, page);
                    mm::kernel_frames().release(frames, pages);
                    co_return false;
                }
            }
            if (image.tls_filesz != 0) {
                auto* destination = static_cast<u8*>(mm::kernel_frames().to_virt(frames));
                auto  read = co_await fs::read_exact(
                    file, astia::span<u8>(destination,
                                          static_cast<usize>(image.tls_filesz)),
                    image.tls_offset);
                if (!read) {
                    unmap_run(map, user_tls_base, pages);
                    mm::kernel_frames().release(frames, pages);
                    co_return false;
                }
            }
            auto* destination = static_cast<u8*>(mm::kernel_frames().to_virt(frames));
            tp_out            = user_tls_base + tls_span;
            *reinterpret_cast<u64*>(destination + tls_span) = tp_out;
            frames_out                                  = frames;
            pages_out                                   = pages;
            co_return true;
        }
    }

    core::task<bool> process::load_user_file_async(
        const mm::shptr<fs::open_file>& file, usize length, u64* entry_out) {
        if (!file || length == 0 || entry_out == nullptr || address_space_ != nullptr)
            co_return false;
        arch::page_map map = arch::page_map::create(arch::kernel_page_map());
        if (!map.valid())
            co_return false;

        stream_context stream{{&map, user_image_base}, file};
        auto loaded = co_await elf::load_executable_stream(
            elf::async_env{&stream, map_user_stream, nullptr, read_stream_async,
                           copy_image, unmap_user_stream},
            length);
        if (!loaded) {
            cleanup_pending(stream.map, map);
            co_return false;
        }
        elf::image image = loaded.value;

        address_space_.reset(new arch::page_map(static_cast<arch::page_map&&>(map)));
        if (address_space_ == nullptr) {
            cleanup_pending(stream.map, map);
            co_return false;
        }
        for (u32 i = 0; i < stream.map.count; ++i) {
            auto& seg    = stream.map.segs[i];
            auto* region = mm::phys_region::adopt(seg.frames, seg.pages);
            if (region == nullptr ||
                !register_mapping(seg.virt, seg.pages, segment_prot(image, seg.virt),
                                  region)) {
                if (region != nullptr) {
                    region->release();
                    seg.used = false;
                }
                cleanup_pending(stream.map, *address_space_);
                discard_image();
                co_return false;
            }
            seg.used = false;
        }
        if (image.tls_memsz != 0) {
            u64 tls_frames = 0, tls_pages = 0, tp = 0;
            auto tls = co_await map_tls_file_async(*address_space_, file, image, tp,
                                                    tls_frames, tls_pages);
            if (!tls || !tls.value) {
                cleanup_pending(stream.map, *address_space_);
                discard_image();
                co_return false;
            }
            auto* region = mm::phys_region::adopt(tls_frames, tls_pages);
            if (region == nullptr ||
                !register_mapping(user_tls_base, tls_pages,
                                  map_protection::read | map_protection::write, region)) {
                if (region != nullptr)
                    region->release();
                else {
                    unmap_run(*address_space_, user_tls_base, tls_pages);
                    mm::kernel_frames().release(tls_frames, tls_pages);
                }
                if (region != nullptr)
                    unmap_run(*address_space_, user_tls_base, tls_pages);
                cleanup_pending(stream.map, *address_space_);
                discard_image();
                co_return false;
            }
            tls_tp_ = tp;
        }
        if (image.tls_memsz == 0)
            tls_tp_ = 0;
        *entry_out = reinterpret_cast<u64>(image.entry);
        co_return true;
    }

} // namespace astia::proc
