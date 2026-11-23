#include "handler_registry.hpp"
#include "support/test.hpp"

ASTIA_TEST(reserved_socket_ids_are_absent_from_dispatch_manifests) {
    ASTIA_CHECK(astia::sys::production::descriptor_ids[0] == 0);
    ASTIA_CHECK(sizeof(astia::sys::production::descriptor_ids) /
                    sizeof(astia::sys::production::descriptor_ids[0]) ==
                70);
    ASTIA_CHECK(sizeof(astia::sys::identity::descriptor_ids) /
                    sizeof(astia::sys::identity::descriptor_ids[0]) ==
                1);
    ASTIA_CHECK(astia::sys::identity::descriptor_ids[0] == 82);

    for (const auto id : astia::sys::production::descriptor_ids)
        ASTIA_CHECK(!astia::sys::is_reserved_socket_syscall(id));
    for (const auto id : astia::sys::identity::descriptor_ids)
        ASTIA_CHECK(!astia::sys::is_reserved_socket_syscall(id));

    for (astia::u32 id = astia::sys::reserved_socket_syscall_first;
         id <= astia::sys::reserved_socket_syscall_last; ++id)
        ASTIA_CHECK(astia::sys::is_reserved_socket_syscall(id));
    ASTIA_CHECK(!astia::sys::is_reserved_socket_syscall(69));
    ASTIA_CHECK(!astia::sys::is_reserved_socket_syscall(82));

    ASTIA_CHECK(static_cast<astia::u32>(astia::sys::syscall_id::uname) == 82);
    ASTIA_CHECK(astia::sys::next_public_syscall_id == 83);
}
