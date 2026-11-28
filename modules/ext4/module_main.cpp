#include "ext4.hpp"

#include <astia/api/errno.h>
#include <astia/core/task.hpp>
#include <astia/error.hpp>
#include <astia/log.hpp>
#include <astia/module.hpp>

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
        if (!registration.valid())
            return;

        const long rc = fs::unregister_type(registration);

        if (rc == 0 || rc == -ENOENT) {
            registration = {};

            return;
        }

        // A live mount still points at this module's code and mount vtables;
        // never let the loader unmap it.
        logs::error("filesystem unregister failed; staying resident");
        (void) astia::module::make_resident();
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
