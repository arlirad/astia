#include "driver.hpp"

#include <astia/error.hpp>

namespace astia::eth {
    const char* driver::name() const {
        return "eth";
    }

    bool driver::match(const net::netdev& device) const {
        return device.kind() == net::link_kind::ethernet;
    }

    core::task<mm::shptr<net::link>>
    driver::create(net::netdev& device, const mm::shptr<net::netdev>& reference) {
        for (u32 i = 0; i < max_links; ++i) {
            if (_used[i])
                continue;

            if (!_links[i].attach(device, reference))
                continue;

            _used[i] = true;

            co_return mm::shptr<net::link>::borrowed(&_links[i]);
        }

        co_return mm::shptr<net::link>{};
    }

    void driver::destroy_now(net::link& value) {
        for (u32 i = 0; i < max_links; ++i) {
            if (&_links[i] != &value)
                continue;

            _links[i].detach();

            _used[i] = false;

            break;
        }
    }

    core::task<void> driver::destroy(net::link& value) {
        destroy_now(value);

        co_return error_t::enone;
    }
} // namespace astia::eth
