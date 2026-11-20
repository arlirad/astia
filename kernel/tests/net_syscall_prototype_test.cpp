#include <astia/api/errno.h>

#include "handler_registry.hpp"
#include "support/test.hpp"
#include "syscall.hpp"

namespace {
    template <typename List>
    struct dispatch_list;

    template <typename... handlers>
    struct dispatch_list<astia::sys::type_list<handlers...>> {
        static bool run(astia::sys::syscall_id id, astia::sys::syscall_context& context,
                        astia::core::completion<astia::i64>& completion) {
            return astia::sys::dispatch_async<handlers...>(id, context, completion);
        }
    };

    struct duplicate_socket_syscall
        : astia::sys::syscall_handler<duplicate_socket_syscall,
                                      static_cast<astia::sys::syscall_id>(80),
                                      astia::sys::syscall_mode::async> {
        static astia::core::task<astia::i64> invoke(astia::sys::syscall_context&) {
            co_return 0;
        }
    };

    struct out_of_range_socket_syscall
        : astia::sys::syscall_handler<out_of_range_socket_syscall,
                                      static_cast<astia::sys::syscall_id>(
                                          astia::sys::syscall_table_capacity),
                                      astia::sys::syscall_mode::async> {
        static astia::core::task<astia::i64> invoke(astia::sys::syscall_context&) {
            co_return 0;
        }
    };

    static_assert(
        !astia::sys::detail::valid_descriptor_ids<
            astia::net::syscall_handlers::socket_syscall, duplicate_socket_syscall>());
    static_assert(
        !astia::sys::detail::valid_descriptor_ids<out_of_range_socket_syscall>());
}

ASTIA_TEST(net_socket_handlers_are_private_and_async) {
    astia::arch::syscall_frame frame{};
    for (astia::u32 number = 70; number < 82; ++number) {
        const auto                  id = static_cast<astia::sys::syscall_id>(number);
        astia::sys::syscall_context context{id, {0, 0, 0, 0, 0, 0}, frame, {}, {}};
        astia::core::completion<astia::i64> completion;
        ASTIA_CHECK(
            dispatch_list<astia::sys::net::handler_set>::run(id, context, completion));
        ASTIA_CHECK(completion.done());
        ASTIA_CHECK(completion.await_resume().value == -ENOSYS);
    }
}

ASTIA_TEST(net_socket_completion_is_exactly_once) {
    astia::core::completion<astia::i64> completed;
    ASTIA_CHECK(completed.complete(7));
    ASTIA_CHECK(!completed.complete(8));
    ASTIA_CHECK(!completed.cancel());
    ASTIA_CHECK(completed.await_resume().value == 7);

    astia::core::completion<astia::i64> canceled;
    ASTIA_CHECK(canceled.cancel());
    ASTIA_CHECK(!canceled.cancel());
    ASTIA_CHECK(!canceled.complete(9));
    ASTIA_CHECK(canceled.done());
}
