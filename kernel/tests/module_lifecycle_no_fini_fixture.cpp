#include <astia/core/task.hpp>
astia::core::task<void> module_main() {
    co_return astia::error_t::enone;
}
