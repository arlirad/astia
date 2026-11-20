#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include "handler_registry.hpp"
#include "support/test.hpp"
#include "syscall.hpp"

namespace {
    struct sync_probe
        : astia::sys::syscall_handler<sync_probe,
                                      static_cast<astia::sys::syscall_id>(126),
                                      astia::sys::syscall_mode::sync> {
        static astia::i64 invoke(astia::sys::syscall_context& context) {
            return context.arguments[0] + 1;
        }
    };

    struct async_probe
        : astia::sys::syscall_handler<async_probe,
                                      static_cast<astia::sys::syscall_id>(127),
                                      astia::sys::syscall_mode::async> {
        static astia::core::task<astia::i64> invoke(astia::sys::syscall_context&) {
            co_return 42;
        }
    };

    struct duplicate_probe
        : astia::sys::syscall_handler<duplicate_probe,
                                      static_cast<astia::sys::syscall_id>(126),
                                      astia::sys::syscall_mode::sync> {
        static astia::i64 invoke(astia::sys::syscall_context&) {
            return 0;
        }
    };

    struct out_of_range_probe
        : astia::sys::syscall_handler<out_of_range_probe,
                                      static_cast<astia::sys::syscall_id>(
                                          astia::sys::syscall_table_capacity),
                                      astia::sys::syscall_mode::sync> {
        static astia::i64 invoke(astia::sys::syscall_context&) {
            return 0;
        }
    };

    static_assert(astia::sys::detail::valid_descriptor_ids<sync_probe, async_probe>());
    static_assert(
        !astia::sys::detail::valid_descriptor_ids<sync_probe, duplicate_probe>());
    static_assert(!astia::sys::detail::valid_descriptor_ids<out_of_range_probe>());

    template <typename List>
    struct dispatch_list;

    template <typename... handlers>
    struct dispatch_list<astia::sys::type_list<handlers...>> {
        static bool run(astia::sys::syscall_id id, astia::sys::syscall_context& context,
                        astia::core::completion<astia::i64>& completion) {
            return astia::sys::dispatch_async<handlers...>(id, context, completion);
        }
    };
}

ASTIA_TEST(production_has_a_separate_async_handler_for_each_public_syscall_id) {
    static_assert(static_cast<astia::u32>(astia::sys::syscall_id::getpid) == SYS_GETPID);
    static_assert(static_cast<astia::u32>(astia::sys::syscall_id::fchdir) == SYS_FCHDIR);
    astia::arch::syscall_frame frame{};
    for (astia::u32 number = 0; number < 70; ++number) {
        const auto                  id = static_cast<astia::sys::syscall_id>(number);
        astia::sys::syscall_context context{id, {0, 0, 0, 0, 0, 0}, frame, {}, {}};
        astia::core::completion<astia::i64> completion;
        ASTIA_CHECK(dispatch_list<astia::sys::production::handler_set>::run(id, context,
                                                                            completion));
        ASTIA_CHECK(completion.done());
        ASTIA_CHECK(completion.await_resume().value == -ENOSYS);
    }
}

ASTIA_TEST(production_unknown_id_is_rejected_without_abi_extension) {
    astia::arch::syscall_frame          frame{};
    const auto                          id = static_cast<astia::sys::syscall_id>(128);
    astia::sys::syscall_context         context{id, {0, 0, 0, 0, 0, 0}, frame, {}, {}};
    astia::core::completion<astia::i64> completion;
    ASTIA_CHECK(!dispatch_list<astia::sys::production::handler_set>::run(id, context,
                                                                         completion));
    ASTIA_CHECK(completion.await_resume().value == -ENOSYS);
}

ASTIA_TEST(production_completion_is_exactly_once) {
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

ASTIA_TEST(mode_specializations_dispatch_without_runtime_mode_selection) {
    astia::arch::syscall_frame  frame{};
    astia::sys::syscall_context context{
        static_cast<astia::sys::syscall_id>(126), {6, 0, 0, 0, 0, 0}, frame, {}, {}};
    astia::core::completion<astia::i64> sync_result;
    ASTIA_CHECK(astia::sys::dispatch_async<sync_probe>(context.id, context, sync_result));
    ASTIA_CHECK(sync_result.await_resume().value == 7);

    context.id = static_cast<astia::sys::syscall_id>(127);
    astia::core::completion<astia::i64> async_result;
    ASTIA_CHECK(
        astia::sys::dispatch_async<async_probe>(context.id, context, async_result));
    ASTIA_CHECK(async_result.await_resume().value == 42);
}
