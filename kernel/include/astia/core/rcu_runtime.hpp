#pragma once

#include <astia/core/rcu_domain.hpp>

namespace astia::core {

    using system_rcu_type = rcu_domain<256, 256>;

    system_rcu_type& system_rcu_domain();
    void             system_rcu_quiescent(u32 cpu);
    u32              system_rcu_poll();

} // namespace astia::core
