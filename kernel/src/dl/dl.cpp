#include "dl.hpp"

#include <astia/memory.hpp>
#include <astia/mm/frame_allocator.hpp>

#include "proc/events.hpp"
#include "proc/exec_hooks.hpp"
#include "proc/process.hpp"

#include <new>

namespace astia::dl {

    namespace {

        using proc::map_protection;
        using proc::process;

        /// Images the registry can hold at once.
        constexpr u32 max_images = 24;

        /// Bindings one process can hold.
        constexpr u32 max_bindings = 8;

        /// Every image is placed on this boundary, so any PT_LOAD `p_align`
        /// up to it keeps its congruence.
        constexpr u64 alignment = 2ull * 1024 * 1024;

        mm::shptr<image> g_images[max_images]{};
        u32              g_image_count = 0;
        u64              g_next_base   = proc::user_mmap_top;
        bool             g_loading     = false;
        core::spinlock   g_lock;

        struct binding {
            mm::shptr<image> img;
            u64              base    = 0;
            u32              handles = 0;
        };

        struct row {
            process* owner = nullptr;
            u32      count = 0;
            binding  items[max_bindings]{};
        };

        row g_rows[proc::max_processes]{};

        u64 page_align_up(u64 value) {
            const u64 page = mm::phys_region::frame_size;
            return (value + page - 1) & ~(page - 1);
        }

        u64 page_align_down(u64 value) {
            const u64 page = mm::phys_region::frame_size;
            return value & ~(page - 1);
        }

        /// Translate typed ELF segment flags to the kernel's mapping
        /// vocabulary.  The two bit orders are deliberately different.
        map_protection protection_of(elf_segment_flag flags) {
            map_protection prot = map_protection::none;
            if (has(flags, elf_segment_flag::read))
                prot = prot | map_protection::read;
            if (has(flags, elf_segment_flag::write))
                prot = prot | map_protection::write;
            if (has(flags, elf_segment_flag::execute))
                prot = prot | map_protection::exec;
            return prot;
        }

        mm::shptr<image> find_image(u64 mount, u64 inode, u64 size) {
            for (u32 i = 0; i < g_image_count; ++i) {
                auto& value = g_images[i];
                if (value && value->ident_mount == mount && value->ident_inode == inode &&
                    value->ident_size == size)
                    return value;
            }
            return {};
        }

        row* find_row(process* owner) {
            for (auto& value : g_rows)
                if (value.owner == owner)
                    return &value;
            return nullptr;
        }

        row* make_row(process* owner) {
            for (auto& value : g_rows)
                if (value.owner == nullptr) {
                    value.owner = owner;
                    return &value;
                }
            return nullptr;
        }

        /// Remove an image whose last mapping is gone.  The registry's
        /// reference is dropped here; the caller's handle keeps the object
        /// alive until it goes out of scope.
        void unlink_image(image* value) {
            for (u32 i = 0; i < g_image_count; ++i) {
                if (g_images[i].get() != value)
                    continue;
                g_images[i]                 = g_images[g_image_count - 1];
                g_images[g_image_count - 1] = {};
                --g_image_count;
                break;
            }
        }

        void drop_ref(image* value) {
            core::spinlock_guard guard(g_lock);
            if (--value->map_count == 0)
                unlink_image(value);
        }

        struct load_context {
            mm::shptr<fs::open_file> file;
            core::cltoken            token;
            u64                      base   = 0;
            u64                      frames = mm::frame_allocator::invalid_frame;
            usize                    pages  = 0;
            u64                      mapped = 0;
            usize                    span   = 0;
        };

        void* map_span(void* opaque, u64 hint, usize size, usize align, bool fixed) {
            auto& context = *static_cast<load_context*>(opaque);
            if (context.frames != mm::frame_allocator::invalid_frame || size == 0 ||
                align == 0 || (align & (align - 1)) != 0 || fixed || align > alignment ||
                (context.base % alignment) != 0)
                return nullptr;
            const u64 pages = (static_cast<u64>(size) + mm::phys_region::frame_size - 1) /
                              mm::phys_region::frame_size;
            const u64 frames = mm::kernel_frames().alloc(pages, mm::frame_flags::zeroed);
            if (frames == mm::frame_allocator::invalid_frame)
                return nullptr;
            context.frames = frames;
            context.pages  = static_cast<usize>(pages);
            context.mapped = context.base + hint;
            context.span   = size;
            return reinterpret_cast<void*>(context.mapped);
        }

        bool copy_span(void* opaque, u64 address, span<const u8> source) {
            auto& context = *static_cast<load_context*>(opaque);
            if (address < context.mapped || address - context.mapped > context.span ||
                source.size() > context.span - (address - context.mapped))
                return false;
            auto* destination =
                static_cast<u8*>(mm::kernel_frames().to_virt(context.frames));
            memcpy(destination + (address - context.mapped), source.data(),
                   source.size());
            return true;
        }

        void unmap_span(void* opaque, void*, usize) {
            auto& context = *static_cast<load_context*>(opaque);
            if (context.frames != mm::frame_allocator::invalid_frame) {
                mm::kernel_frames().release(context.frames, context.pages);
                context.frames = mm::frame_allocator::invalid_frame;
            }
        }

        core::task<i64> read_span(void* opaque, span<u8> destination, u64 offset) {
            auto& context = *static_cast<load_context*>(opaque);
            auto  result =
                co_await context.file->read_exact(destination, offset, context.token);
            if (!result)
                co_return result.error;
            co_return result.value;
        }

        core::task<i64> read_to_span(void* opaque, u64 address, usize length,
                                     u64 offset) {
            auto& context = *static_cast<load_context*>(opaque);
            if (address < context.mapped || address - context.mapped > context.span ||
                length > context.span - (address - context.mapped))
                co_return error_t::efault;
            auto* destination =
                static_cast<u8*>(mm::kernel_frames().to_virt(context.frames)) +
                (address - context.mapped);
            auto result = co_await context.file->read_exact(span<u8>(destination, length),
                                                            offset, context.token);
            if (!result)
                co_return result.error;
            co_return result.value;
        }

        core::task<mm::shptr<image>> load_image(mm::shptr<fs::open_file> file, u64 mount,
                                                u64 inode, u64 size, core::cltoken token,
                                                u64 base) {
            auto value = mm::share<image>();
            if (!value)
                co_return error_t::enomem;

            load_context context{static_cast<mm::shptr<fs::open_file>&&>(file), token};
            context.base = base;

            elf::loader::async_env environment{&context,  map_span,  nullptr,
                                               read_span, copy_span, unmap_span};
            environment.read_to = read_to_span;
            auto loaded = co_await elf::loader::load_executable_stream(environment, size);
            if (!loaded) {
                unmap_span(&context, nullptr, 0);
                co_return loaded.error;
            }

            auto& result = loaded.value;
            if (result.nsegs == 0 || result.phdr_vaddr == 0 || result.interp_size != 0) {
                unmap_span(&context, nullptr, 0);
                co_return error_t::einval;
            }

            auto* region = mm::phys_region::adopt(context.frames, context.pages);
            if (region == nullptr) {
                unmap_span(&context, nullptr, 0);
                co_return error_t::enomem;
            }
            context.frames = mm::frame_allocator::invalid_frame;

            const u64 span_start = reinterpret_cast<u64>(result.base) - result.load_bias;
            const u64 page_bytes = mm::phys_region::frame_size;

            value->region        = region;
            value->base          = result.load_bias;
            value->span          = result.size;
            value->entry         = reinterpret_cast<u64>(result.entry);
            value->phdr          = result.phdr_vaddr;
            value->phnum         = result.phnum;
            value->phent         = result.phent;
            value->ident_mount   = mount;
            value->ident_inode   = inode;
            value->ident_size    = size;
            value->segment_count = static_cast<u32>(result.nsegs);

            bool valid = true;
            for (u32 i = 0; i < value->segment_count; ++i) {
                const auto& source = result.segs[i];
                const u64   start  = page_align_down(source.vaddr);
                const u64   end    = page_align_up(source.vaddr + source.memsz);
                if (end <= start || start < span_start ||
                    (start - span_start) % page_bytes != 0 || end - start > result.size) {
                    valid = false;
                    break;
                }
                const auto flags = mapper<elf_segment_flag>::from(source.flags);
                if (!flags) {
                    valid = false;
                    break;
                }
                value->segments[i] = {
                    start,
                    end - start,
                    protection_of(flags.value),
                    static_cast<u32>((start - span_start) / page_bytes),
                };
            }
            if (!valid) {
                region->release();
                co_return error_t::einval;
            }
            co_return value;
        }

        bool map_into(process& target, image* value) {
            const u64 page_bytes = mm::phys_region::frame_size;
            u32       mapped     = 0;
            for (; mapped < value->segment_count; ++mapped) {
                auto&      item  = value->segments[mapped];
                const auto pages = static_cast<usize>(item.bytes / page_bytes);
                if (!value->region->share_range(item.region_page, pages))
                    break;
                if (!target.map_shared_range(
                        value->base + item.vaddr, pages, item.prot, value->region,
                        item.region_page, proc::has(item.prot, map_protection::write))) {
                    value->region->release_range(item.region_page, pages);
                    break;
                }
            }
            if (mapped == value->segment_count)
                return true;
            for (u32 i = 0; i < mapped; ++i)
                (void) target.unmap(value->base + value->segments[i].vaddr,
                                    value->segments[i].bytes);
            return false;
        }

    } // namespace

    namespace {

        core::task<proc::interpreter_info> load_for_exec(process&    target,
                                                         const char* path) {
            if (path == nullptr)
                co_return error_t::einval;
            auto file = fs::open_path(path);
            if (!file)
                co_return error_t::enoent;
            if (file->size() <= 0)
                co_return error_t::einval;
            auto mapped = co_await map(target, file, 0);
            if (!mapped)
                co_return mapped.error;
            co_return proc::interpreter_info{mapped.value.base, mapped.value.entry};
        }

        void release_bindings(process& target) {
            release(target);
        }

        void adopt_bindings(process& target, process& staging) {
            adopt(target, staging);
        }

    } // namespace

    bool init() {
        if (!proc::register_exec_hooks(
                {&load_for_exec, &release_bindings, &adopt_bindings}))
            return false;
        proc::events::teardown::add("dl", core::hook::priority::late,
                                    [](proc::process& target) {
                                        release(target);
                                        return core::hook::outcome::pass;
                                    });
        proc::events::forked::add("dl", core::hook::priority::normal,
                                  [](proc::process& child, proc::process& parent) {
                                      return clone(child, parent)
                                                 ? core::hook::outcome::pass
                                                 : core::hook::outcome::stop;
                                  });
        return true;
    }

    core::task<info> map(process& target, mm::shptr<fs::open_file> file, u32 flags) {
        if (flags != 0)
            co_return error_t::einval;
        if (!file)
            co_return error_t::ebadf;
        const long size = file->size();
        if (size <= 0)
            co_return error_t::einval;

        const u64 mount = reinterpret_cast<u64>(file->mount_owner());
        const u64 inode = file->parent() ? file->parent()->inode() : 0;

        mm::shptr<image> value;
        bool             created = false;
        u64              base    = 0;
        {
            core::spinlock_guard guard(g_lock);
            if (auto* row = find_row(&target)) {
                for (u32 i = 0; i < row->count; ++i) {
                    if (row->items[i].img->ident_mount == mount &&
                        row->items[i].img->ident_inode == inode &&
                        row->items[i].img->ident_size == static_cast<u64>(size)) {
                        ++row->items[i].handles;
                        co_return info{
                            row->items[i].img->base,  row->items[i].img->span,
                            row->items[i].img->entry, row->items[i].img->phdr,
                            row->items[i].img->phnum, row->items[i].img->phent};
                    }
                }
            }
            value = find_image(mount, inode, static_cast<u64>(size));
            if (value) {
                if (value->loading)
                    co_return error_t::eagain;
                ++value->map_count;
            }
            else {
                if (g_loading || g_image_count == max_images)
                    co_return error_t::eagain;
                g_loading = true;
                base      = g_next_base;
                created   = true;
            }
        }

        if (created) {
            auto loaded = co_await load_image(file, mount, inode, static_cast<u64>(size),
                                              target.io_token(), base);
            if (!loaded) {
                core::spinlock_guard guard(g_lock);
                g_loading = false;
                co_return loaded.error;
            }
            value = loaded.value;

            const u64 advance =
                (page_align_up(value->span) + alignment - 1) & ~(alignment - 1);
            mm::phys_region* rejected = nullptr;
            {
                core::spinlock_guard guard(g_lock);
                g_loading = false;
                if (g_image_count == max_images || advance >= proc::user_tls_base ||
                    g_next_base > proc::user_tls_base - advance) {
                    rejected = value->region;
                }
                else {
                    value->map_count = 1;
                    value->loading   = false;
                    g_next_base += advance;
                    g_images[g_image_count++] = value;
                }
            }
            if (rejected != nullptr) {
                rejected->release();
                co_return error_t::enomem;
            }
        }

        if (!map_into(target, value.get())) {
            drop_ref(value.get());
            co_return error_t::enomem;
        }

        {
            core::spinlock_guard guard(g_lock);
            row*                 owner = find_row(&target);
            if (owner == nullptr)
                owner = make_row(&target);
            if (owner == nullptr || owner->count == max_bindings) {
                for (u32 i = 0; i < value->segment_count; ++i)
                    (void) target.unmap(value->base + value->segments[i].vaddr,
                                        value->segments[i].bytes);
                drop_ref(value.get());
                co_return error_t::enospc;
            }
            auto& item   = owner->items[owner->count++];
            item.img     = value;
            item.base    = value->base;
            item.handles = 1;
        }
        co_return info{value->base, value->span,  value->entry,
                       value->phdr, value->phnum, value->phent};
    }

    i64 unmap(process& target, u64 base) {
        mm::shptr<image> value;
        {
            core::spinlock_guard guard(g_lock);
            auto*                row = find_row(&target);
            if (row == nullptr)
                return -EINVAL;
            for (u32 i = 0; i < row->count; ++i) {
                if (row->items[i].base != base)
                    continue;
                if (row->items[i].handles > 1) {
                    --row->items[i].handles;
                    return 0;
                }
                value                  = row->items[i].img;
                row->items[i]          = row->items[--row->count];
                row->items[row->count] = {};
                break;
            }
            if (!value)
                return -EINVAL;
        }
        for (u32 i = 0; i < value->segment_count; ++i)
            (void) target.unmap(value->base + value->segments[i].vaddr,
                                value->segments[i].bytes);
        drop_ref(value.get());
        return 0;
    }

    void release(process& target) {
        core::spinlock_guard guard(g_lock);
        auto*                row = find_row(&target);
        if (row == nullptr)
            return;
        for (u32 i = 0; i < row->count; ++i) {
            auto* value = row->items[i].img.get();
            if (--value->map_count == 0)
                unlink_image(value);
        }
        row->owner = nullptr;
        row->count = 0;
        for (auto& item : row->items)
            item = {};
    }

    void adopt(process& target, process& staging) {
        core::spinlock_guard guard(g_lock);
        auto*                source = find_row(&staging);
        if (source == nullptr || source->count == 0)
            return;
        auto* destination = find_row(&target);
        if (destination == nullptr)
            destination = make_row(&target);
        if (destination == nullptr || destination->count != 0)
            return;
        *destination       = *source;
        destination->owner = &target;
        source->owner      = nullptr;
        source->count      = 0;
        for (auto& item : source->items)
            item = {};
    }

    bool clone(process& child, process& parent) {
        core::spinlock_guard guard(g_lock);
        auto*                source = find_row(&parent);
        if (source == nullptr || source->count == 0)
            return true;
        if (source->count > max_bindings)
            return false;
        auto* destination = find_row(&child);
        if (destination != nullptr)
            return false;
        destination = make_row(&child);
        if (destination == nullptr)
            return false;
        for (u32 i = 0; i < source->count; ++i) {
            ++source->items[i].img->map_count;
            destination->items[destination->count++] = {source->items[i].img,
                                                        source->items[i].base, 1};
        }
        return true;
    }

    image::~image() {
        if (region != nullptr)
            region->release();
    }

} // namespace astia::dl
