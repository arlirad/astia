#pragma once

#include <astia/mm/shptr.hpp>
#include <astia/net/netdev.hpp>

namespace astia::net {
    class registry final {
        public:
        static constexpr u32 max_devices = 32;

        bool register_device(netdev& device) {
            for (u32 i = 0; i < _count; ++i) {
                if (_devices[i].get() == &device)
                    return true;
            }

            if (!device.valid() || _count == max_devices)
                return false;

            _devices[_count++] = mm::shptr<netdev>::borrowed(&device);

            return true;
        }

        bool unregister_device(netdev& device) {
            for (u32 i = 0; i < _count; ++i) {
                if (_devices[i].get() != &device)
                    continue;

                _devices[i]      = _devices[--_count];
                _devices[_count] = {};

                return true;
            }

            return false;
        }

        netdev* at(u32 index) const {
            return index < _count ? _devices[index].get() : nullptr;
        }

        u32 count() const {
            return _count;
        }

        private:
        mm::shptr<netdev> _devices[max_devices]{};
        u32               _count = 0;
    };

    registry& devices();
} // namespace astia::net
