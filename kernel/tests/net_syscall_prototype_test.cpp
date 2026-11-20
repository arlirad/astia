#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include "support/test.hpp"
#include "syscall.hpp"
#include "syscall_prototype/net/syscalls/accept.hpp"
#include "syscall_prototype/net/syscalls/bind.hpp"
#include "syscall_prototype/net/syscalls/connect.hpp"
#include "syscall_prototype/net/syscalls/getsockopt.hpp"
#include "syscall_prototype/net/syscalls/listen.hpp"
#include "syscall_prototype/net/syscalls/recv.hpp"
#include "syscall_prototype/net/syscalls/recvfrom.hpp"
#include "syscall_prototype/net/syscalls/send.hpp"
#include "syscall_prototype/net/syscalls/sendto.hpp"
#include "syscall_prototype/net/syscalls/shutdown.hpp"
#include "syscall_prototype/net/syscalls/socket.hpp"
#include "syscall_prototype/net/syscalls/socketpair.hpp"

namespace {
    using namespace astia;

    struct duplicate_socket_handler
        : sys::handler<duplicate_socket_handler, static_cast<sys::syscall_id>(80)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(0);
        }
    };

    static_assert(
        sys::detail::valid_descriptor_ids<
            net::syscall_prototype::AcceptHandler, net::syscall_prototype::BindHandler,
            net::syscall_prototype::ConnectHandler,
            net::syscall_prototype::GetsockoptHandler,
            net::syscall_prototype::ListenHandler, net::syscall_prototype::RecvHandler,
            net::syscall_prototype::RecvfromHandler, net::syscall_prototype::SendHandler,
            net::syscall_prototype::SendtoHandler,
            net::syscall_prototype::ShutdownHandler,
            net::syscall_prototype::SocketHandler,
            net::syscall_prototype::SocketpairHandler>());
    static_assert(!sys::detail::valid_descriptor_ids<
                  net::syscall_prototype::SocketHandler, duplicate_socket_handler>());

    constexpr sys::syscall_id ids[] = {
        static_cast<sys::syscall_id>(70), static_cast<sys::syscall_id>(71),
        static_cast<sys::syscall_id>(72), static_cast<sys::syscall_id>(73),
        static_cast<sys::syscall_id>(74), static_cast<sys::syscall_id>(75),
        static_cast<sys::syscall_id>(76), static_cast<sys::syscall_id>(77),
        static_cast<sys::syscall_id>(78), static_cast<sys::syscall_id>(79),
        static_cast<sys::syscall_id>(80), static_cast<sys::syscall_id>(81)};

}

ASTIA_TEST(net_socket_prototype_ids_are_owned_and_async) {
    static_assert(static_cast<u32>(sys::syscall_id::getpid) == SYS_GETPID);
    static_assert(static_cast<u32>(sys::syscall_id::fchdir) == SYS_FCHDIR);
    arch::syscall_frame frame{};
    for (usize i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        sys::syscall_context  context{ids[i], {0, 0, 0, 0, 0, 0}, frame, 0, 0};
        core::completion<i64> completion;
        ASTIA_CHECK((sys::dispatch_async<net::syscall_prototype::AcceptHandler,
                                         net::syscall_prototype::BindHandler,
                                         net::syscall_prototype::ConnectHandler,
                                         net::syscall_prototype::GetsockoptHandler,
                                         net::syscall_prototype::ListenHandler,
                                         net::syscall_prototype::RecvHandler,
                                         net::syscall_prototype::RecvfromHandler,
                                         net::syscall_prototype::SendHandler,
                                         net::syscall_prototype::SendtoHandler,
                                         net::syscall_prototype::ShutdownHandler,
                                         net::syscall_prototype::SocketHandler,
                                         net::syscall_prototype::SocketpairHandler>(
            ids[i], context, completion)));
        ASTIA_CHECK(completion.done());
        const auto result = completion.await_resume();
        ASTIA_CHECK(result && result.value == -EINPROGRESS);
    }
}

ASTIA_TEST(net_socket_prototype_completion_and_cancel_are_exactly_once) {
    core::completion<i64> completed;
    ASTIA_CHECK(completed.complete(7));
    ASTIA_CHECK(!completed.complete(8));
    ASTIA_CHECK(!completed.cancel());
    ASTIA_CHECK(completed.await_resume().value == 7);

    core::completion<i64> canceled;
    ASTIA_CHECK(canceled.cancel());
    ASTIA_CHECK(!canceled.cancel());
    ASTIA_CHECK(!canceled.complete(9));
    ASTIA_CHECK(canceled.done());
}

ASTIA_TEST(net_socket_prototype_unknown_ids_return_enosys) {
    arch::syscall_frame   frame{};
    const auto            id = static_cast<sys::syscall_id>(128);
    sys::syscall_context  context{id, {0, 0, 0, 0, 0, 0}, frame, 0, 0};
    core::completion<i64> completion;
    ASTIA_CHECK(
        !(sys::dispatch_async<
            net::syscall_prototype::AcceptHandler, net::syscall_prototype::BindHandler,
            net::syscall_prototype::ConnectHandler,
            net::syscall_prototype::GetsockoptHandler,
            net::syscall_prototype::ListenHandler, net::syscall_prototype::RecvHandler,
            net::syscall_prototype::RecvfromHandler, net::syscall_prototype::SendHandler,
            net::syscall_prototype::SendtoHandler,
            net::syscall_prototype::ShutdownHandler,
            net::syscall_prototype::SocketHandler,
            net::syscall_prototype::SocketpairHandler>(id, context, completion)));
    ASTIA_CHECK(completion.await_resume().value == -ENOSYS);
}
