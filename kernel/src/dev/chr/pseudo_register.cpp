#include <astia/fs.hpp>
#include <astia/mm/share.hpp>

#include "../device_file.hpp"
#include "pseudo.hpp"

namespace astia::dev {
    namespace {
        bool register_pseudo(const char* name, fs::file* file) {
            fs::device_node_descriptor descriptor{};

            descriptor.name        = name;
            descriptor.node        = mm::shptr<fs::file>::borrowed(file);
            descriptor.type        = fs::file_type::character;
            descriptor.permissions = fs::default_device_permissions;
            descriptor.capabilities =
                static_cast<u32>(fs::device_node_capability::readable) |
                static_cast<u32>(fs::device_node_capability::writable) |
                static_cast<u32>(fs::device_node_capability::pollable) |
                static_cast<u32>(fs::device_node_capability::ioctlable);

            fs::device_node_handle handle{};

            return fs::register_device_node(descriptor, handle) == 0;
        }
    } // namespace

    bool pseudo_devices_init() {
        static constinit null_device    null{};
        static constinit zero_device    zero{};
        static constinit urandom_device urandom{};

        static constinit device_file null_file{&null};
        static constinit device_file zero_file{&zero};
        static constinit device_file urandom_file{&urandom};

        return register_pseudo("null", &null_file) &&
               register_pseudo("zero", &zero_file) &&
               register_pseudo("urandom", &urandom_file);
    }
} // namespace astia::dev
