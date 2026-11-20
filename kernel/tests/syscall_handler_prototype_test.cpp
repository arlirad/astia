#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include "support/test.hpp"
#include "syscall.hpp"
#include "syscall_prototype/registry.hpp"

namespace {
    template <typename List>
    struct dispatch_list;

    template <typename... Handlers>
    struct dispatch_list<astia::sys::prototype::type_list<Handlers...>> {
        static bool run(astia::sys::syscall_id id, astia::sys::syscall_context& context,
                        astia::core::completion<astia::i64>& completion) {
            return astia::sys::dispatch_async<Handlers...>(id, context, completion);
        }
    };

    constexpr astia::sys::syscall_id public_ids[] = {
        static_cast<astia::sys::syscall_id>(0),  static_cast<astia::sys::syscall_id>(1),
        static_cast<astia::sys::syscall_id>(2),  static_cast<astia::sys::syscall_id>(3),
        static_cast<astia::sys::syscall_id>(4),  static_cast<astia::sys::syscall_id>(5),
        static_cast<astia::sys::syscall_id>(6),  static_cast<astia::sys::syscall_id>(7),
        static_cast<astia::sys::syscall_id>(8),  static_cast<astia::sys::syscall_id>(9),
        static_cast<astia::sys::syscall_id>(10), static_cast<astia::sys::syscall_id>(11),
        static_cast<astia::sys::syscall_id>(12), static_cast<astia::sys::syscall_id>(13),
        static_cast<astia::sys::syscall_id>(14), static_cast<astia::sys::syscall_id>(15),
        static_cast<astia::sys::syscall_id>(16), static_cast<astia::sys::syscall_id>(17),
        static_cast<astia::sys::syscall_id>(18), static_cast<astia::sys::syscall_id>(19),
        static_cast<astia::sys::syscall_id>(20), static_cast<astia::sys::syscall_id>(21),
        static_cast<astia::sys::syscall_id>(22), static_cast<astia::sys::syscall_id>(23),
        static_cast<astia::sys::syscall_id>(24), static_cast<astia::sys::syscall_id>(25),
        static_cast<astia::sys::syscall_id>(26), static_cast<astia::sys::syscall_id>(27),
        static_cast<astia::sys::syscall_id>(28), static_cast<astia::sys::syscall_id>(29),
        static_cast<astia::sys::syscall_id>(30), static_cast<astia::sys::syscall_id>(31),
        static_cast<astia::sys::syscall_id>(32), static_cast<astia::sys::syscall_id>(33),
        static_cast<astia::sys::syscall_id>(34), static_cast<astia::sys::syscall_id>(35),
        static_cast<astia::sys::syscall_id>(36), static_cast<astia::sys::syscall_id>(37),
        static_cast<astia::sys::syscall_id>(38), static_cast<astia::sys::syscall_id>(39),
        static_cast<astia::sys::syscall_id>(40), static_cast<astia::sys::syscall_id>(41),
        static_cast<astia::sys::syscall_id>(42), static_cast<astia::sys::syscall_id>(43),
        static_cast<astia::sys::syscall_id>(44), static_cast<astia::sys::syscall_id>(45),
        static_cast<astia::sys::syscall_id>(46), static_cast<astia::sys::syscall_id>(47),
        static_cast<astia::sys::syscall_id>(48), static_cast<astia::sys::syscall_id>(49),
        static_cast<astia::sys::syscall_id>(50), static_cast<astia::sys::syscall_id>(51),
        static_cast<astia::sys::syscall_id>(52), static_cast<astia::sys::syscall_id>(53),
        static_cast<astia::sys::syscall_id>(54), static_cast<astia::sys::syscall_id>(55),
        static_cast<astia::sys::syscall_id>(56), static_cast<astia::sys::syscall_id>(57),
        static_cast<astia::sys::syscall_id>(58), static_cast<astia::sys::syscall_id>(59),
        static_cast<astia::sys::syscall_id>(60), static_cast<astia::sys::syscall_id>(61),
        static_cast<astia::sys::syscall_id>(62), static_cast<astia::sys::syscall_id>(63),
        static_cast<astia::sys::syscall_id>(64), static_cast<astia::sys::syscall_id>(65),
        static_cast<astia::sys::syscall_id>(66), static_cast<astia::sys::syscall_id>(67),
        static_cast<astia::sys::syscall_id>(68), static_cast<astia::sys::syscall_id>(69)};
}

ASTIA_TEST(prototype_has_a_separate_async_handler_for_each_public_syscall_id) {
    static_assert(static_cast<astia::u32>(astia::sys::syscall_id::getpid) == SYS_GETPID);
    static_assert(static_cast<astia::u32>(astia::sys::syscall_id::io_submit) ==
                  SYS_IO_SUBMIT);
    static_assert(static_cast<astia::u32>(astia::sys::syscall_id::fchdir) == SYS_FCHDIR);
    astia::arch::syscall_frame frame{};
    for (const auto id : public_ids) {
        astia::sys::syscall_context         context{id, {0, 0, 0, 0, 0, 0}, frame, 0, 0};
        astia::core::completion<astia::i64> completion;
        ASTIA_CHECK(dispatch_list<astia::sys::prototype::handler_set>::run(id, context,
                                                                           completion));
        ASTIA_CHECK(completion.done());
        const auto result = completion.await_resume();
        ASTIA_CHECK(result && result.value == -EINPROGRESS);
    }
}

ASTIA_TEST(prototype_unknown_id_is_rejected_without_abi_extension) {
    astia::arch::syscall_frame          frame{};
    const auto                          id = static_cast<astia::sys::syscall_id>(128);
    astia::sys::syscall_context         context{id, {0, 0, 0, 0, 0, 0}, frame, 0, 0};
    astia::core::completion<astia::i64> completion;
    ASTIA_CHECK(
        !dispatch_list<astia::sys::prototype::handler_set>::run(id, context, completion));
    ASTIA_CHECK(completion.await_resume().value == -ENOSYS);
}
