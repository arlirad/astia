#pragma once

#include <astia/core/task.hpp>
#include <astia/mm/shptr.hpp>
#include <astia/net/link_driver.hpp>

#include "link.hpp"

namespace astia::eth {
    /// Creates Ethernet links for Ethernet netdevs.  The links are statically
    /// owned by the driver; the bus holds borrowed shared pointers.
    class driver final : public net::link_driver {
        public:
        const char* name() const override;
        bool        match(const net::netdev& device) const override;
        core::task<mm::shptr<net::link>>
        create(net::netdev& device, const mm::shptr<net::netdev>& reference) override;
        core::task<void> destroy(net::link& value) override;
        void             destroy_now(net::link& value) override;

        private:
        static constexpr u32 max_links = 4;

        link _links[max_links];
        bool _used[max_links]{};
    };
} // namespace astia::eth
