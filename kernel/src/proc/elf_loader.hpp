#pragma once

#include <astia/fs.hpp>
#include <astia/core/task.hpp>
#include <astia/elf.hpp>

namespace astia::proc {

    core::task<elf::image> load_elf(const mm::shptr<fs::open_file>& file, usize length,
                                     const elf::env& environment);

} // namespace astia::proc
