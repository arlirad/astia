#include <astia/api/syscalls.h>
#include <astia/elf.hpp>
#include <astia/fs.hpp>
#include <astia/mm/frame_allocator.hpp>

#include "exec_hooks.hpp"
#include "exec_rollback.hpp"
#include "proc.hpp"
#include "process.hpp"
#include "tls_arena.hpp"

namespace astia::proc {

    namespace {
        bool is_power_of_two(u64 value) {
            return value != 0 && (value & (value - 1)) == 0;
        }

        bool checked_align_up(u64 value, u64 alignment, u64& result) {
            if (!is_power_of_two(alignment) || value > ~u64{0} - (alignment - 1))
                return false;
            result = (value + alignment - 1) & ~(alignment - 1);
            return true;
        }

        /// One PT_LOAD segment's physical run, recorded at map time so
        /// the load can file it as a VMA afterwards.
        struct mapped_segment {
            u64  virt   = 0;
            u64  frames = 0;
            u64  pages  = 0;
            bool used   = false;
            bool mapped = false;
        };

        struct image_map {
            mm::address_space* map;
            u64                next_base;
            mapped_segment     segs[elf::loader::max_segments]{};
            u32                count = 0;
        };

        struct stream_context {
            image_map                map;
            mm::shptr<fs::open_file> file;
            core::cltoken            token;

            ~stream_context();
        };

        void unmap_run(mm::address_space& map, u64 virt, u64 pages) {
            for (u64 page = 0; page < pages; ++page) {
                (void) map.unmap(virt + page * elf::loader::page_size);
            }
        }

        void cleanup_pending(image_map& image, mm::address_space& map) {
            for (u32 i = 0; i < image.count; ++i) {
                auto& seg = image.segs[i];
                if (!seg.used) {
                    continue;
                }
                if (seg.mapped) {
                    unmap_run(map, seg.virt, seg.pages);
                    seg.mapped = false;
                }
                mm::kernel_frames().release(seg.frames, seg.pages);
                seg.used = false;
            }
        }

        stream_context::~stream_context() {
            if (map.map != nullptr)
                cleanup_pending(map, *map.map);
        }

        void* map_user(void* opaque, u64 hint, usize size, usize align, bool fixed) {
            auto& image = *static_cast<image_map*>(opaque);
            if (align == 0 || (align & (align - 1)) != 0)
                return nullptr;
            u64 base = hint;
            if (!fixed) {
                base              = image.next_base;
                const u64 residue = hint & (align - 1);
                const u64 current = base & (align - 1);
                const u64 delta   = (residue + align - current) & (align - 1);
                if (base > ~u64{0} - delta)
                    return nullptr;
                base += delta;
            }
            if (base < user_image_base || base + size >= user_address_limit ||
                image.count >= elf::loader::max_segments) {
                return nullptr;
            }

            // One contiguous frame run per segment: the VMA that files
            // this range adopts it as a single phys_region — the unit
            // fork's reference counting and CoW splits work in.
            const u64 pages =
                (size + elf::loader::page_size - 1) / elf::loader::page_size;
            const u64 frames = mm::kernel_frames().alloc(pages, mm::frame_flags::zeroed);
            if (frames == mm::frame_allocator::invalid_frame) {
                return nullptr;
            }
            for (u64 page = 0; page < pages; ++page) {
                if (!image.map->map_4k(base + page * elf::loader::page_size,
                                       frames + page * elf::loader::page_size,
                                       mm::mapping_flags::present |
                                           mm::mapping_flags::writable |
                                           mm::mapping_flags::user)) {
                    unmap_run(*image.map, base, page);
                    mm::kernel_frames().release(frames, pages);
                    return nullptr;
                }
            }
            image.segs[image.count] = mapped_segment{base, frames, pages, true, true};
            ++image.count;
            image.next_base = base + size + elf::loader::page_size;
            return reinterpret_cast<void*>(base);
        }

        bool copy_image(void* opaque, u64 address, span<const u8> source) {
            auto& image = *static_cast<image_map*>(opaque);
            for (u32 i = 0; i < image.count; ++i) {
                const auto& seg = image.segs[i];
                if (address < seg.virt ||
                    address - seg.virt > seg.pages * elf::loader::page_size ||
                    source.size() >
                        seg.pages * elf::loader::page_size - (address - seg.virt))
                    continue;
                auto* destination =
                    static_cast<u8*>(mm::kernel_frames().to_virt(seg.frames));
                __builtin_memcpy(destination + (address - seg.virt), source.data(),
                                 source.size());
                return true;
            }
            return false;
        }

        bool stream_destination(image_map& image, u64 address, usize length,
                                u8*& destination) {
            for (u32 i = 0; i < image.count; ++i) {
                const auto& seg  = image.segs[i];
                const u64   span = seg.pages * elf::loader::page_size;
                if (address < seg.virt || address - seg.virt > span ||
                    length > span - (address - seg.virt))
                    continue;
                destination = static_cast<u8*>(mm::kernel_frames().to_virt(seg.frames)) +
                              (address - seg.virt);
                return true;
            }
            return false;
        }

        void unmap_user(void* opaque, void* address, usize size) {
            auto& image = *static_cast<image_map*>(opaque);
            for (u32 i = 0; i < image.count; ++i) {
                auto& segment = image.segs[i];
                if (segment.used && segment.virt == reinterpret_cast<u64>(address) &&
                    segment.pages * elf::loader::page_size == size) {
                    unmap_run(*image.map, segment.virt, segment.pages);
                    segment.mapped = false;
                    mm::kernel_frames().release(segment.frames, segment.pages);
                    segment.used = false;
                    return;
                }
            }
        }

        void rollback_unmap(void* context, u64 address, usize pages) {
            unmap_run(*static_cast<mm::address_space*>(context), address, pages);
        }

        void rollback_release_frames(void*, u64 frames, u64 pages) {
            mm::kernel_frames().release(frames, pages);
        }

        void rollback_release_region(void* region) {
            static_cast<mm::phys_region*>(region)->release();
        }

        void rollback_mapping_failure(mm::address_space& map, u64 address, usize pages,
                                      u64 frames, mm::phys_region* region) {
            detail::rollback_mapping(&map, address, pages, frames, region, rollback_unmap,
                                     rollback_release_frames, rollback_release_region);
        }

        // The ELF loader reserves and maps one contiguous span for all
        // PT_LOAD segments.  Until VMAs can split that span at each
        // segment boundary, keep the span executable and writable:
        // this preserves the loader's established mapping contract and
        // lets fork/CoW account for it as one physical region.  The
        // page-granular permission split is a later mprotect/exec slice.
        map_protection segment_prot(const elf::loader::image& image, u64 base) {
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
        bool tls_layout(const elf::loader::image& image, u64& tls_span,
                        tls_arena& arena) {
            const u64 align = image.tls_align == 0 ? 1 : image.tls_align;
            if (image.tls_filesz > image.tls_memsz || !is_power_of_two(align) ||
                !checked_align_up(image.tls_memsz, align, tls_span) ||
                tls_span > user_address_limit - user_tls_base ||
                tls_span > ~u64{0} - user_tls_tcb_headroom ||
                tls_span + user_tls_tcb_headroom > ~u64{0} - (elf::loader::page_size - 1))
                return false;
            const u64 payload_bytes =
                tls_span + user_tls_tcb_headroom + elf::loader::page_size - 1;
            return measure_tls_arena(
                static_cast<usize>(payload_bytes / elf::loader::page_size), arena);
        }

        bool map_tls_template(mm::address_space& map, span<const u8> bytes,
                              const elf::loader::image& image, u64& tp_out,
                              mm::phys_region*& region_out) {
            u64       tls_span = 0;
            tls_arena arena{};
            if (!tls_layout(image, tls_span, arena))
                return false;
            if (image.tls_offset > bytes.size() ||
                image.tls_filesz > bytes.size() - image.tls_offset)
                return false;
            const auto source = astia::span<const u8>(
                bytes.data() + image.tls_offset, static_cast<usize>(image.tls_filesz));
            auto* region = create_tls_arena(arena);
            if (region == nullptr)
                return false;
            const u64 bottom = user_tls_base - user_dso_tls_reserve;
            const u64 first  = bottom + arena.reserve_pages * elf::loader::page_size;
            usize     mapped = 0;
            auto      fail   = [&]() {
                unmap_run(map, first, mapped);
                region->release();
                return false;
            };
            for (usize page = arena.reserve_pages; page < arena.total_pages; ++page) {
                if (!map.map_4k(bottom + page * elf::loader::page_size,
                                region->page_base(page),
                                mm::mapping_flags::present | mm::mapping_flags::writable |
                                    mm::mapping_flags::user)) {
                    return fail();
                }
                ++mapped;
            }
            if (!write_tls_payload(
                    *region, arena, 0,
                    astia::span<const u8>(source.data(),
                                          static_cast<usize>(image.tls_filesz)))) {
                return fail();
            }
            tp_out = user_tls_base + tls_span;
            if (!write_tls_word(*region, arena, tls_span, tp_out))
                return fail();
            region_out = region;
            return true;
        }
    } // namespace

    void process::rollback_tls_failure(void* context, mm::address_space& map) {
        auto& image = *static_cast<image_map*>(context);
        struct transaction_context {
            process*           owner;
            image_map*         image;
            mm::address_space* map;
        } transaction{this, &image, &map};
        detail::rollback_mapping_entry entries[elf::loader::max_segments]{};
        for (u32 i = 0; i < image.count; ++i) {
            auto& segment = image.segs[i];
            entries[i]    = {segment.virt,    static_cast<usize>(segment.pages),
                             segment.frames,  nullptr,
                             &segment.mapped, &segment.used,
                             segment.used};
        }
        auto cleanup = [](void* value) {
            auto& transaction = *static_cast<transaction_context*>(value);
            cleanup_pending(*transaction.image, *transaction.map);
        };
        auto discard = [](void* value) {
            static_cast<transaction_context*>(value)->owner->discard_image();
        };
        detail::rollback_image_transaction(
            &transaction, entries, image.count,
            [](void* value, u64 address, usize pages) {
                auto& transaction = *static_cast<transaction_context*>(value);
                unmap_run(*transaction.map, address, pages);
            },
            [](void*, u64 frames, u64 pages) {
                mm::kernel_frames().release(frames, pages);
            },
            [](void* region) { static_cast<mm::phys_region*>(region)->release(); },
            cleanup, discard);
    }

    bool process::load_user_image(span<const u8> bytes, u64* entry_out) {
        if (bytes.null() || bytes.empty() || entry_out == nullptr ||
            _address_space.attached() || !mm::address_space::kernel().valid()) {
            return false;
        }

        mm::address_space map = mm::address_space::create();
        if (!map.valid()) {
            return false;
        }
        image_map        image_state{&map, user_image_base};
        elf::loader::env environment{};
        environment.ctx   = &image_state;
        environment.map   = map_user;
        environment.unmap = unmap_user;
        environment.copy  = copy_image;
        elf::loader::image image{};

        // The ELF implementation copies segment bytes through the virtual
        // addresses returned by map_fn, so the child map must be active while
        // it performs the load. Kernel-half mappings are shared by create().
        // Whichever map was active on the way in comes back on the way out:
        // spawnve may be running inside a ring-3 process's syscall, and
        // dropping to the kernel map there faults the caller's very next
        // user instruction.
        const process*           caller = current_process();
        const mm::address_space* caller_map =
            caller == nullptr ? nullptr : caller->address_space();
        map.activate();
        const bool loaded = elf::loader::load_executable(bytes, environment, image);
        if (caller_map != nullptr) {
            caller_map->activate();
        }
        else {
            mm::address_space::kernel().activate();
        }
        if (!loaded) {
            cleanup_pending(image_state, map);
            return false;
        }

        _address_space = static_cast<mm::address_space&&>(map);
        if (!_address_space.attached()) {
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
                rollback_mapping_failure(_address_space, seg.virt, seg.pages, seg.frames,
                                         region);
                image_state.segs[i].mapped = false;
                image_state.segs[i].used   = false;
                cleanup_pending(image_state, _address_space);
                discard_image();
                return false;
            }
            image_state.segs[i].used = false;
        }

        // Static TLS: the thread's %fs base loads the block end this
        // materializes (0 = the image has no PT_TLS and nothing ever
        // reaches %fs).
        if (image.tls_memsz != 0 || image.interp_size != 0) {
            u64              tp         = 0;
            mm::phys_region* tls_region = nullptr;
            if (!map_tls_template(_address_space, bytes, image, tp, tls_region)) {
                rollback_tls_failure(&image_state, _address_space);
                return false;
            }
            _tls_tp               = tp;
            _exec_image.tls_base  = user_tls_base;
            _exec_image.tls_tp    = tp;
            _exec_image.tls_limit = user_tls_base - user_dso_tls_reserve;
            const u64 tls_pages   = tls_region->pages();
            if (!register_mapping(user_tls_base - user_dso_tls_reserve, tls_pages,
                                  map_protection::read | map_protection::write,
                                  tls_region)) {
                rollback_mapping_failure(_address_space,
                                         user_tls_base - user_dso_tls_reserve, tls_pages,
                                         0, tls_region);
                rollback_tls_failure(&image_state, _address_space);
                return false;
            }
        }

        // The synchronous path has no task context to load an interpreter
        // in; exec_user_image() is kernel-internal and only drives static
        // test images.
        if (image.interp_size != 0)
            return false;
        _exec_image.entry  = reinterpret_cast<u64>(image.entry);
        _exec_image.phdr   = image.phdr_vaddr;
        _exec_image.interp = 0;
        _exec_image.phnum  = image.phnum;
        _exec_image.phent  = image.phent;
        *entry_out         = reinterpret_cast<u64>(image.entry);
        return true;
    }

    namespace {
        core::task<i64> read_stream_async(void* opaque, span<u8> destination,
                                          u64 offset) {
            auto* context = static_cast<stream_context*>(opaque);
            if (context == nullptr || !context->file)
                co_return error_t::ebadf;
            auto result =
                co_await context->file->read_exact(destination, offset, context->token);
            if (!result)
                co_return result.error;
            co_return result.value;
        }

        core::task<i64> read_stream_to_async(void* opaque, u64 address, usize length,
                                             u64 offset) {
            auto* context = static_cast<stream_context*>(opaque);
            if (context == nullptr || !context->file)
                co_return error_t::ebadf;
            u8* destination = nullptr;
            if (!stream_destination(context->map, address, length, destination))
                co_return error_t::efault;
            auto result = co_await context->file->read_exact(
                span<u8>(destination, length), offset, context->token);
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
                    segment.pages * elf::loader::page_size == size) {
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

        core::task<bool> map_tls_file_async(mm::address_space&        map,
                                            mm::shptr<fs::open_file>  file,
                                            core::cltoken             token,
                                            const elf::loader::image& image, u64& tp_out,
                                            mm::phys_region*& region_out) {
            u64       tls_span = 0;
            tls_arena arena{};
            if (!tls_layout(image, tls_span, arena))
                co_return false;
            auto* region = create_tls_arena(arena);
            if (region == nullptr)
                co_return false;
            const u64 bottom  = user_tls_base - user_dso_tls_reserve;
            const u64 first   = bottom + arena.reserve_pages * elf::loader::page_size;
            usize     mapped  = 0;
            auto      cleanup = [&]() {
                unmap_run(map, first, mapped);
                region->release();
            };
            for (usize page = arena.reserve_pages; page < arena.total_pages; ++page) {
                if (!map.map_4k(bottom + page * elf::loader::page_size,
                                region->page_base(page),
                                mm::mapping_flags::present | mm::mapping_flags::writable |
                                    mm::mapping_flags::user)) {
                    cleanup();
                    co_return false;
                }
                ++mapped;
            }
            // Lazy payload pages are separate frames, so feed the file read
            // one page-sized window at a time instead of one contiguous span.
            u64 done = 0;
            while (done < image.tls_filesz) {
                const u64   within = arena.payload_offset + done;
                const usize page   = static_cast<usize>(within / elf::loader::page_size);
                const usize offset = static_cast<usize>(within % elf::loader::page_size);
                usize       chunk  = elf::loader::page_size - offset;
                if (chunk > image.tls_filesz - done)
                    chunk = static_cast<usize>(image.tls_filesz - done);
                auto* destination = static_cast<u8*>(region->page_direct(page));
                if (destination == nullptr) {
                    cleanup();
                    co_return false;
                }
                auto read = co_await file->read_exact(
                    astia::span<u8>(destination + offset, chunk), image.tls_offset + done,
                    token);
                if (!read) {
                    cleanup();
                    co_return false;
                }
                done += chunk;
            }
            tp_out = user_tls_base + tls_span;
            if (!write_tls_word(*region, arena, tls_span, tp_out)) {
                cleanup();
                co_return false;
            }
            region_out = region;
            co_return true;
        }
    }

    core::task<bool> process::load_user_file_async(mm::shptr<fs::open_file> file,
                                                   usize length, u64* entry_out) {
        if (!file || length == 0 || entry_out == nullptr || _address_space.attached())
            co_return false;
        mm::address_space map = mm::address_space::create();
        if (!map.valid())
            co_return false;

        stream_context stream{{&map, user_image_base}, file, _cancellation_root.token()};
        elf::loader::async_env environment{&stream,    map_user_stream,
                                           nullptr,    read_stream_async,
                                           copy_image, unmap_user_stream};
        environment.read_to = read_stream_to_async;
        auto loaded = co_await elf::loader::load_executable_stream(environment, length);
        if (!loaded) {
            cleanup_pending(stream.map, map);
            co_return false;
        }
        elf::loader::image image = loaded.value;

        _address_space = static_cast<mm::address_space&&>(map);
        if (!_address_space.attached()) {
            cleanup_pending(stream.map, map);
            co_return false;
        }
        for (u32 i = 0; i < stream.map.count; ++i) {
            auto& seg    = stream.map.segs[i];
            auto* region = mm::phys_region::adopt(seg.frames, seg.pages);
            if (region == nullptr ||
                !register_mapping(seg.virt, seg.pages, segment_prot(image, seg.virt),
                                  region)) {
                rollback_mapping_failure(_address_space, seg.virt, seg.pages, seg.frames,
                                         region);
                seg.mapped = false;
                seg.used   = false;
                cleanup_pending(stream.map, _address_space);
                discard_image();
                co_return false;
            }
            seg.used = false;
        }
        if (image.tls_memsz != 0 || image.interp_size != 0) {
            u64              tp         = 0;
            mm::phys_region* tls_region = nullptr;
            auto             tls        = co_await map_tls_file_async(
                _address_space, file, _cancellation_root.token(), image, tp, tls_region);
            if (!tls || !tls.value) {
                rollback_tls_failure(&stream.map, _address_space);
                co_return false;
            }
            const u64 tls_pages = tls_region->pages();
            if (!register_mapping(user_tls_base - user_dso_tls_reserve, tls_pages,
                                  map_protection::read | map_protection::write,
                                  tls_region)) {
                rollback_mapping_failure(_address_space,
                                         user_tls_base - user_dso_tls_reserve, tls_pages,
                                         0, tls_region);
                rollback_tls_failure(&stream.map, _address_space);
                co_return false;
            }
            _tls_tp               = tp;
            _exec_image.tls_base  = user_tls_base;
            _exec_image.tls_tp    = tp;
            _exec_image.tls_limit = user_tls_base - user_dso_tls_reserve;
        }
        // With an interpreter the arena is mapped even when the payload has
        // no TLS: shared objects lay their blocks out below it.
        if (image.tls_memsz == 0 && image.interp_size == 0)
            _tls_tp = 0;

        _exec_image.entry  = reinterpret_cast<u64>(image.entry);
        _exec_image.phdr   = image.phdr_vaddr;
        _exec_image.interp = 0;
        _exec_image.phnum  = image.phnum;
        _exec_image.phent  = image.phent;

        u64 entry = reinterpret_cast<u64>(image.entry);
        if (image.interp_size != 0) {
            if (image.interp_size >= fs::path_max)
                co_return false;
            char path[fs::path_max]{};
            auto read = co_await file->read_exact(
                span<u8>(reinterpret_cast<u8*>(path), image.interp_size),
                image.interp_offset, _cancellation_root.token());
            if (!read || read.value != static_cast<i64>(image.interp_size) ||
                path[image.interp_size - 1] != '\0')
                co_return false;
            auto interp = co_await load_interpreter(*this, path);
            if (!interp)
                co_return false;
            _exec_image.interp = interp.value.base;
            entry              = interp.value.entry;
        }
        *entry_out = entry;
        co_return true;
    }

} // namespace astia::proc
