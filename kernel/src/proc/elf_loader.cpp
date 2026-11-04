#include "elf_loader.hpp"

#include <astia/mm/heap.hpp>

namespace astia::proc {

    core::task<elf::image> load_elf(const mm::shptr<fs::open_file>& file, usize length,
                                    const elf::env& environment) {
        if (!file)
            co_return error_t::ebadf;
        if (length == 0)
            co_return error_t::einval;

        auto storage = mm::heap::alloc(length);
        if (!storage)
            co_return storage.error;

        auto read = co_await fs::read_exact(
            file, span<u8>(static_cast<u8*>(storage.value), length), 0);
        if (!read) {
            mm::heap::free(storage.value);
            co_return read.error;
        }

        elf::image image{};
        const bool loaded = elf::load(
            span<const u8>(static_cast<const u8*>(storage.value), length), environment, image);
        mm::heap::free(storage.value);
        if (!loaded)
            co_return error_t::eio;
        co_return image;
    }

} // namespace astia::proc
