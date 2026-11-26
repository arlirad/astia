#include <astia/fs.hpp>
#include <astia/mm/share.hpp>

#include "../device_file.hpp"
#include "pseudo.hpp"

namespace astia::dev {
    bool pseudo_devices_init() {
        static constinit null_device    null{};
        static constinit zero_device    zero{};
        static constinit urandom_device urandom{};

        static constinit device_file null_file{&null};
        static constinit device_file zero_file{&zero};
        static constinit device_file urandom_file{&urandom};

        return fs::register_device("/dev/null",
                                   mm::shptr<fs::file>::borrowed(&null_file)) == 0 &&
               fs::register_device("/dev/zero",
                                   mm::shptr<fs::file>::borrowed(&zero_file)) == 0 &&
               fs::register_device("/dev/urandom",
                                   mm::shptr<fs::file>::borrowed(&urandom_file)) == 0;
    }
} // namespace astia::dev
