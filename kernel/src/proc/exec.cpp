#include "proc.hpp"
#include "process.hpp"
#include <astia/api/syscalls.h>
#include <astia/arch/page_map.hpp>
#include <astia/elf.hpp>
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
            image_map      map;
            fs::open_file* file;
        };

        void* map_user(void* opaque, u64 hint, usize size, usize align, bool fixed) {
            auto&     state = *static_cast<image_map*>(opaque);
            const u64 base  = fixed ? hint : (state.next_base + align - 1) & ~(align - 1);
            if (base < user_image_base || base + size >= user_address_limit ||
                state.count >= elf::max_segments) {
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
                if (!state.map->map_4k(
                        base + page * elf::page_size, frames + page * elf::page_size,
                        arch::page_flags::present | arch::page_flags::writable |
                            arch::page_flags::user)) {
                    mm::kernel_frames().release(frames, pages);
                    return nullptr;
                }
            }
            state.segs[state.count] = mapped_segment{base, frames, pages, true};
            ++state.count;
            state.next_base = base + size + elf::page_size;
            return reinterpret_cast<void*>(base);
        }

        bool copy_image(void* opaque, u64 address, const void* source, usize length) {
            auto& state = *static_cast<image_map*>(opaque);
            for (u32 i = 0; i < state.count; ++i) {
                const auto& seg = state.segs[i];
                if (address < seg.virt ||
                    address + length > seg.virt + seg.pages * elf::page_size)
                    continue;
                auto* destination =
                    static_cast<u8*>(mm::kernel_frames().to_virt(seg.frames));
                __builtin_memcpy(destination + (address - seg.virt), source, length);
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
        constexpr u64 tls_base = 0x00007ffff0000000ull;
        // Mapped headroom after the thread pointer: the first word is the
        // self-pointer used by Clang's x86_64 TLS sequence, and the rest is
        // available to the userspace TCB.
        constexpr u64 tls_tcb_headroom = 256;

        // Materialize the PT_TLS template in the child: user pages, the
        // init bytes copied out of the file, the rest zeroed.  The
        // block's end, aligned up, is the x86_64 thread pointer.  The
        // frames come as one contiguous run so the block can be filed
        // as a single VMA (fork shares it copy-on-write); frames_out
        // hands the run back for that adoption.
        bool map_tls_template(arch::page_map& map, const void* bytes,
                              const elf::image& image, u64& tp_out, u64& frames_out,
                              u64& pages_out) {
            const u64 align = image.tls_align == 0 ? 1 : image.tls_align;
            const u64 span  = (image.tls_memsz + align - 1) & ~(align - 1);
            const u64 pages =
                (span + tls_tcb_headroom + elf::page_size - 1) / elf::page_size;
            const auto* source = static_cast<const u8*>(bytes) + image.tls_offset;
            const u64 frames = mm::kernel_frames().alloc(pages, mm::frame_flags::zeroed);
            if (frames == mm::frame_allocator::invalid_frame) {
                return false;
            }
            for (u64 page = 0; page < pages; ++page) {
                if (!map.map_4k(tls_base + page * elf::page_size,
                                frames + page * elf::page_size,
                                arch::page_flags::present | arch::page_flags::writable |
                                    arch::page_flags::user)) {
                    mm::kernel_frames().release(frames, pages);
                    return false;
                }
            }
            auto* destination = static_cast<u8*>(mm::kernel_frames().to_virt(frames));
            for (u64 index = 0; index < image.tls_filesz && index < image.tls_memsz;
                 ++index) {
                destination[index] = source[index];
            }
            tp_out                                      = tls_base + span;
            *reinterpret_cast<u64*>(destination + span) = tp_out;
            frames_out                                  = frames;
            pages_out                                   = pages;
            return true;
        }
    } // namespace

    bool process::load_user_image(const void* bytes, usize length, u64* entry_out) {
        if (bytes == nullptr || length == 0 || entry_out == nullptr ||
            address_space_ != nullptr || arch::kernel_page_map() == nullptr) {
            return false;
        }

        arch::page_map map = arch::page_map::create(arch::kernel_page_map());
        if (!map.valid()) {
            return false;
        }
        image_map  state{&map, user_image_base};
        elf::env   environment{&state, map_user, nullptr, nullptr, copy_image};
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
        const bool loaded = elf::load_executable(bytes, length, environment, image);
        if (caller_map != nullptr) {
            caller_map->activate();
        }
        else {
            arch::kernel_page_map()->activate();
        }
        if (!loaded) {
            return false;
        }

        // File every segment as a VMA: the frames (mapped above)
        // transfer to a phys_region, so fork and copy-on-write
        // account for the image like any other mapping.
        for (u32 i = 0; i < state.count; ++i) {
            const auto& seg = state.segs[i];
            if (!seg.used) {
                continue;
            }
            auto* region = mm::phys_region::adopt(seg.frames, seg.pages);
            if (region == nullptr ||
                !register_mapping(seg.virt, seg.pages, segment_prot(image, seg.virt),
                                  region)) {
                if (region != nullptr) {
                    region->release();
                }
                return false;
            }
        }

        // Static TLS: the thread's %fs base loads the block end this
        // materializes (0 = the image has no PT_TLS and nothing ever
        // reaches %fs).
        if (image.tls_memsz != 0) {
            u64 tp         = 0;
            u64 tls_frames = 0;
            u64 tls_pages  = 0;
            if (!map_tls_template(map, bytes, image, tp, tls_frames, tls_pages)) {
                return false;
            }
            tls_tp_      = tp;
            auto* region = mm::phys_region::adopt(tls_frames, tls_pages);
            if (region == nullptr ||
                !register_mapping(tls_base, tls_pages,
                                  map_protection::read | map_protection::write, region)) {
                if (region != nullptr) {
                    region->release();
                }
                return false;
            }
        }

        address_space_ = new arch::page_map(static_cast<arch::page_map&&>(map));
        if (address_space_ == nullptr) {
            return false;
        }
        *entry_out = reinterpret_cast<u64>(image.entry);
        return true;
    }

    namespace {
        bool read_stream_batch(void* opaque, elf::read_request* requests, usize count) {
            auto* context = static_cast<stream_context*>(opaque);
            if (context == nullptr || context->file == nullptr)
                return false;
            fs::kernel_read batch[elf::max_segments]{};
            if (count > elf::max_segments)
                return false;
            for (usize i = 0; i < count; ++i) {
                batch[i] = {context->file, requests[i].buffer, requests[i].offset,
                            static_cast<u32>(requests[i].length), -1};
            }
            if (!fs::read_batch(batch, count))
                return false;
            for (usize i = 0; i < count; ++i)
                requests[i].result = batch[i].result;
            return true;
        }

        void* map_user_stream(void* opaque, u64 hint, usize size, usize align,
                              bool fixed) {
            return map_user(&static_cast<stream_context*>(opaque)->map, hint, size, align,
                            fixed);
        }

        bool map_tls_file(arch::page_map& map, const mm::shptr<fs::open_file>& file,
                          const elf::image& image, u64& tp_out, u64& frames_out,
                          u64& pages_out) {
            const u64 align = image.tls_align == 0 ? 1 : image.tls_align;
            const u64 span  = (image.tls_memsz + align - 1) & ~(align - 1);
            const u64 pages =
                (span + tls_tcb_headroom + elf::page_size - 1) / elf::page_size;
            const u64 frames = mm::kernel_frames().alloc(pages, mm::frame_flags::zeroed);
            if (frames == mm::frame_allocator::invalid_frame)
                return false;
            for (u64 page = 0; page < pages; ++page) {
                if (!map.map_4k(tls_base + page * elf::page_size,
                                frames + page * elf::page_size,
                                arch::page_flags::present | arch::page_flags::writable |
                                    arch::page_flags::user)) {
                    mm::kernel_frames().release(frames, pages);
                    return false;
                }
            }
            if (image.tls_filesz != 0) {
                auto*      destination = mm::kernel_frames().to_virt(frames);
                const long read        = file->read(
                    destination, static_cast<u32>(image.tls_filesz), image.tls_offset);
                if (read != static_cast<long>(image.tls_filesz)) {
                    mm::kernel_frames().release(frames, pages);
                    return false;
                }
            }
            auto* destination = static_cast<u8*>(mm::kernel_frames().to_virt(frames));
            tp_out            = tls_base + span;
            *reinterpret_cast<u64*>(destination + span) = tp_out;
            frames_out                                  = frames;
            pages_out                                   = pages;
            return true;
        }
    }

    bool process::load_user_file(const mm::shptr<fs::open_file>& file, usize length,
                                 u64* entry_out) {
        if (!file || length == 0 || entry_out == nullptr || address_space_ != nullptr)
            return false;
        arch::page_map map = arch::page_map::create(arch::kernel_page_map());
        if (!map.valid())
            return false;
        stream_context        stream{{&map, user_image_base}, file.get()};
        elf::image            image{};
        const process*        caller = current_process();
        const arch::page_map* caller_map =
            caller == nullptr ? nullptr : caller->address_space();
        map.activate();
        const bool loaded = elf::load_executable_stream(
            elf::env{&stream, map_user_stream, nullptr, read_stream_batch}, length,
            image);
        if (caller_map != nullptr)
            caller_map->activate();
        else
            arch::kernel_page_map()->activate();
        if (!loaded)
            return false;
        for (u32 i = 0; i < stream.map.count; ++i) {
            const auto& seg    = stream.map.segs[i];
            auto*       region = mm::phys_region::adopt(seg.frames, seg.pages);
            if (region == nullptr ||
                !register_mapping(seg.virt, seg.pages, segment_prot(image, seg.virt),
                                  region))
                return false;
        }
        if (image.tls_memsz != 0) {
            u64 tls_frames = 0, tls_pages = 0, tp = 0;
            if (!map_tls_file(map, file, image, tp, tls_frames, tls_pages))
                return false;
            auto* region = mm::phys_region::adopt(tls_frames, tls_pages);
            if (region == nullptr ||
                !register_mapping(tls_base, tls_pages,
                                  map_protection::read | map_protection::write, region)) {
                if (region != nullptr)
                    region->release();
                return false;
            }
            tls_tp_ = tp;
        }
        address_space_ = new arch::page_map(static_cast<arch::page_map&&>(map));
        if (address_space_ == nullptr)
            return false;
        if (image.tls_memsz == 0)
            tls_tp_ = 0;
        *entry_out = reinterpret_cast<u64>(image.entry);
        return true;
    }

} // namespace astia::proc
