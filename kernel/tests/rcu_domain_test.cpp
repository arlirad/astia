#include "support/test.hpp"
#include <astia/core/rcu_domain.hpp>

namespace {
    astia::u32 destroyed = 0;
    void destroy_int(void* value) {
        delete static_cast<int*>(value);
        ++destroyed;
    }
}

ASTIA_TEST(rcu_domain_waits_for_reader_grace_period) {
    astia::core::rcu_domain<4, 8> domain;
    int* value = new int(42);
    ASTIA_CHECK(domain.retire(value, destroy_int));
    {
        astia::core::rcu_domain<4, 8>::read_guard reader(domain, 1);
        ASTIA_CHECK(reader.active());
        ASTIA_CHECK(domain.poll() == 0);
        ASTIA_CHECK(domain.pending() == 1);
    }
    ASTIA_CHECK(domain.poll() == 1);
    ASTIA_CHECK(domain.pending() == 0);
}

ASTIA_TEST(rcu_domain_supports_nested_readers) {
    astia::core::rcu_domain<2, 4> domain;
    int* value = new int(7);
    {
        astia::core::rcu_domain<2, 4>::read_guard outer(domain, 0);
        astia::core::rcu_domain<2, 4>::read_guard inner(domain, 0);
        ASTIA_CHECK(domain.retire(value, destroy_int));
        ASTIA_CHECK(domain.poll() == 0);
    }
    ASTIA_CHECK(domain.poll() == 1);
}
