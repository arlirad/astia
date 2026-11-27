#pragma once

#include <astia/core/task.hpp>
#include <astia/mm/shptr.hpp>
#include <astia/net.hpp>
#include <astia/net/link.hpp>

namespace astia::net {
    /// Creates the L2 implementation for a netdev.  create() returns a link
    /// already associated with the device; destroy() detaches it.  Both are
    /// task-shaped and must not be called with the bus lock held.
    class link_driver {
        public:
        virtual ~link_driver() = default;

        virtual const char* name() const                      = 0;
        virtual bool        match(const netdev& device) const = 0;
        virtual core::task<mm::shptr<link>>
        create(netdev& device, const mm::shptr<netdev>& reference) = 0;
        virtual core::task<void> destroy(link& value)              = 0;

        /// Synchronous teardown used from module fini context.  Must not
        /// suspend or wait for hardware; it only detaches and releases the
        /// link's software state.
        virtual void destroy_now(link& value) = 0;
    };

    core::task<bool> register_link_driver(link_driver& driver);
    core::task<void> unregister_link_driver(link_driver& driver);

    /// Fini-context variant: unbinds and removes the driver without waiting.
    void unregister_link_driver_now(link_driver& driver);
} // namespace astia::net
