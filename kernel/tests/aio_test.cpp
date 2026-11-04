#include "support/test.hpp"
#ifdef RENAME_NOREPLACE
#undef RENAME_NOREPLACE
#endif
#ifdef RENAME_EXCHANGE
#undef RENAME_EXCHANGE
#endif
#include <astia/api/aio.h>

ASTIA_TEST(aio_cq_consume_acknowledges_notification_generation) {
    struct fixture {
        astia_rings   ring{};
        astia_cq_ring cq{};
    } value;

    value.ring.cq_off = sizeof(astia_rings);
    value.cq.notify_generation = 7;
    astia_cq_consume(&value.ring, 1);

    ASTIA_CHECK(value.cq.head == 1);
    ASTIA_CHECK(value.cq.notify_ack == 7);

    value.cq.notify_generation = 8;
    astia_cq_consume(&value.ring, 0);
    ASTIA_CHECK(value.cq.notify_ack == 7);
}
