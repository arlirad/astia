#include "support/test.hpp"
#ifdef RENAME_NOREPLACE
#undef RENAME_NOREPLACE
#endif
#ifdef RENAME_EXCHANGE
#undef RENAME_EXCHANGE
#endif
#include <astia/api/aio.h>

ASTIA_TEST(socket_option_abi_has_posix_socket_names) {
    ASTIA_CHECK(AF_UNSPEC == 0);
    ASTIA_CHECK(AF_UNIX == 1);
    ASTIA_CHECK(SOL_SOCKET == 1);
    ASTIA_CHECK(SO_SNDBUF != SO_RCVBUF);
    ASTIA_CHECK(OP_GETSOCKOPT > OP_FTRUNCATE);
}
