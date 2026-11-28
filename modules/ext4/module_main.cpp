#include "ext4.hpp"

#include <astia/core/task.hpp>
#include <astia/error.hpp>
#include <astia/log.hpp>

namespace astia::ext4 {
    using logs = astia::log::context<"ext4">;

    namespace {
        filesystem                 instance;
        fs::filesystem_registration registration;
    }

    bool register_filesystem() {
        if (registration.valid())
            return false;

        return fs::register_type(&instance, registration) == 0;
    }

    void unregister_filesystem() {
        if (registration.valid())
            (void) fs::unregister_type(registration);

        registration = {};
    }

    [[gnu::destructor]] void ext4_module_fini() {
        unregister_filesystem();
    }
} // namespace astia::ext4

astia::core::task<void> module_main() {
    if (!astia::ext4::register_filesystem()) {
        if (astia::ext4::registration.valid()) {
            astia::ext4::logs::error("filesystem already registered");

            co_return astia::error_t::eexist;
        }

        astia::ext4::logs::error("filesystem registration failed");

        co_return astia::error_t::eio;
    }

    astia::ext4::logs::info("ready");

    co_return astia::error_t::enone;
}
