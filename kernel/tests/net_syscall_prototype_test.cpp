#include <astia/api/errno.h>

#include "handler_registry.hpp"
#include "support/test.hpp"
#include "syscall.hpp"

ASTIA_TEST(net_socket_handlers_are_private_and_async) {
    astia::arch::syscall_frame frame{};
    ASTIA_CHECK(sizeof(astia::sys::net::descriptors) /
                    sizeof(astia::sys::net::descriptors[0]) ==
                12);
    for (astia::u32 number = 70; number < 82; ++number) {
        auto                        id = static_cast<astia::sys::syscall_id>(number);
        astia::sys::syscall_context context{id, {0, 0, 0, 0, 0, 0}, frame, {}, {}};
        astia::core::completion<astia::i64> completion;
        for (const auto& descriptor : astia::sys::net::descriptors) {
            if (descriptor.id == id) {
                descriptor.invoke(context, completion);
                break;
            }
        }
        ASTIA_CHECK(completion.done());
        ASTIA_CHECK(completion.await_resume().value == -ENOSYS);
    }
}
