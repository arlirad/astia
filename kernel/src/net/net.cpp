#include <astia/net.hpp>

namespace astia::net {
    registry& devices() {
        static registry value;

        return value;
    }
} // namespace astia::net
