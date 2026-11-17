#include <astia/net.hpp>

namespace astia::dev::net {

    registry& devices() {
        static registry value;
        return value;
    }

} // namespace astia::dev::net
