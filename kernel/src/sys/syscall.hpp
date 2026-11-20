#pragma once

#include <astia/api/errno.h>
#include <astia/arch/context.hpp>
#include <astia/common.hpp>
#include <astia/core/completion.hpp>
#include <astia/core/task.hpp>
#include <astia/mapping.hpp>
#include <astia/resource_limits.hpp>

#include <type_traits>

namespace astia::sys {

    inline constexpr u32 syscall_table_capacity =
        resource_limits::abi::syscall_table_capacity;

    /// Architecture-independent syscall numbers mirror the public ABI.
    enum class syscall_id : u32 {
        io_setup = 0,
        io_destroy,
        io_submit,
        io_register,
        mmap,
        munmap,
        mprotect,
        clock_gettime,
        exit,
        yield,
        getpid,
        gettid,
        futex,
        waitpid,
        spawnve,
        fork,
        thread_create,
        thread_exit,
        thread_join,
        thread_detach,
        kill,
        sigaction,
        sigprocmask,
        thread_create2,
        execve,
        sigsuspend,
        getpgrp,
        setpgid,
        setsid,
        sigreturn,
        chdir,
        getcwd,
        getuid,
        geteuid,
        getgid,
        getegid,
        setuid,
        seteuid,
        setgid,
        setegid,
        setreuid,
        setregid,
        getgroups,
        setgroups,
        process_snapshot,
        getsid,
        unmount,
        mount,
        getrlimit,
        setrlimit,
        sigwait,
        sigaltstack,
        getpgid,
        getppid,
        thread_cancel         = 54,
        alarm                 = 55,
        thread_create_owned   = 56,
        thread_cancel_pending = 57,
        thread_exists         = 58,
        thread_kill           = 59,
        sigpending            = 60,
        sigtimedwait          = 61,
        clock_getres          = 62,
        thread_getattr        = 63,
        msync                 = 64,
        lseek                 = 65,
        umask                 = 66,
        shm_open              = 67,
        shm_unlink            = 68,
        fchdir                = 69,
    };

    /// How many registers the ABI hands every trap — and so the most
    /// arguments any handler can be given.
    inline constexpr u32 syscall_argument_count = 6;

    /// What the table stores: the six argument registers the ABI passes,
    /// then the trap image those registers came from.  A handler names the
    /// registers it uses — the adapter picks that prefix out of the six —
    /// and one handler takes the image instead: fork, which resumes the
    /// child in the caller's own state and so needs more of the caller
    /// than the arguments say.  Nothing else pays for it: the registers
    /// stay registers for the other nineteen calls.
    using legacy_syscall_handler = i64 (*)(i64 a0, i64 a1, i64 a2, i64 a3, i64 a4, i64 a5,
                                           const arch::syscall_frame& frame);

    /// The shape that one handler has: the image itself, no registers.
    using syscall_image_handler = i64 (*)(const arch::syscall_frame& frame);
    using syscall_exit_hook     = void (*)(arch::syscall_frame& frame, i64 result);
    struct operation_id {
        u64 value;
    };

    struct syscall_user_data {
        u64 value;
    };

    struct syscall_context {
        syscall_id                 id;
        i64                        arguments[syscall_argument_count];
        const arch::syscall_frame& frame;
        operation_id               operation;
        syscall_user_data          user_data;
    };

    using async_syscall_handler = void (*)(syscall_context&       context,
                                           core::completion<i64>& completion);
    using task_syscall_handler  = core::task<i64> (*)(syscall_context& context);

    struct syscall_descriptor {
        syscall_id            id;
        async_syscall_handler invoke;
    };

    namespace detail {

        template <typename T>
        struct is_async_handler {
            static constexpr bool value = false;
        };

        template <>
        struct is_async_handler<async_syscall_handler> {
            static constexpr bool value = true;
        };

        /// One register, by position.  The adapter walks 0..arity-1 of
        /// these, so a handler's parameters are the prefix of the ABI's
        /// registers that it actually asked for.
        template <u32 I>
        constexpr i64 argument(i64 a0, i64 a1, i64 a2, i64 a3, i64 a4, i64 a5) {
            if constexpr (I == 0)
                return a0;
            else if constexpr (I == 1)
                return a1;
            else if constexpr (I == 2)
                return a2;
            else if constexpr (I == 3)
                return a3;
            else if constexpr (I == 4)
                return a4;
            else
                return a5;
        }

        /// 0..N-1 as a pack.  There is no libstdc++ behind the kernel, so
        /// this is the handful of lines std::make_index_sequence would be.
        template <u32... I>
        struct index_sequence {};

        template <u32 N, u32... I>
        struct make_index_sequence : make_index_sequence<N - 1, N - 1, I...> {};

        template <u32... I>
        struct make_index_sequence<0, I...> {
            using type = index_sequence<I...>;
        };

        /// How many arguments a handler declares.  Anything that is not a
        /// function pointer has no arity, and fails the assert below with
        /// the message rather than with a pile of template errors.
        template <typename Fn>
        struct arity {
            static constexpr u32 value = syscall_argument_count + 1;
        };

        template <typename R, typename... A>
        struct arity<R (*)(A...)> {
            static constexpr u32 value = sizeof...(A);
        };

        /// The handler that takes the image needs no registers picked out
        /// of the six, so the adapter hands it the image and nothing else.
        template <typename Fn>
        struct is_image_handler {
            static constexpr bool value = false;
        };

        template <>
        struct is_image_handler<syscall_image_handler> {
            static constexpr bool value = true;
        };

        /// One handler widened to the table's shape.  Parameter n of the
        /// handler is register n, cast to the type the handler declared it
        /// with: a `u32 flags` truncates a junk-high register exactly as
        /// the per-handler casts used to, except the cast is named once
        /// here and not repeated in every body.  The registers a handler
        /// of this arity does not name, and the image it does not want,
        /// are the adapter's business, not the handler's.
        template <auto Fn, typename Signature, typename Index>
        struct adapter;

        template <auto Fn, typename R, typename... A, u32... I>
        struct adapter<Fn, R (*)(A...), index_sequence<I...>> {
            static i64 call([[maybe_unused]] i64 a0, [[maybe_unused]] i64 a1,
                            [[maybe_unused]] i64 a2, [[maybe_unused]] i64 a3,
                            [[maybe_unused]] i64 a4, [[maybe_unused]] i64 a5,
                            [[maybe_unused]] const arch::syscall_frame& frame) {
                if constexpr (is_image_handler<decltype(Fn)>::value) {
                    return static_cast<i64>(Fn(frame));
                }
                else {
                    return static_cast<i64>(
                        Fn(static_cast<A>(argument<I>(a0, a1, a2, a3, a4, a5))...));
                }
            }
        };

        template <auto Fn>
        using widened =
            adapter<Fn, decltype(Fn),
                    typename make_index_sequence<arity<decltype(Fn)>::value>::type>;

        /// The table door: store a handler already in the table's form.
        /// register_syscall() is what callers use; this is what it hands
        /// the table.
        bool register_abi(syscall_id id, legacy_syscall_handler handler);

    } // namespace detail

    bool register_exit_hook(syscall_exit_hook hook);

    template <auto function>
    bool register_syscall(syscall_id id) {
        static_assert(detail::arity<decltype(function)>::value <= syscall_argument_count,
                      "a syscall handler must use no more than the six ABI arguments");
        return detail::register_abi(id, &detail::widened<function>::call);
    }

    enum class syscall_mode : u8 {
        sync,
        async,
    };

    template <class Impl, syscall_id Id, syscall_mode Mode>
    struct syscall_handler;

    template <typename Impl, syscall_id Id>
    struct syscall_handler<Impl, Id, syscall_mode::sync> {
        using syscall_descriptor_marker = void;

        static constexpr syscall_id id = Id;
        static void thunk(syscall_context& context, core::completion<i64>& completion) {
            static_assert(
                std::is_same_v<decltype(&Impl::invoke), i64 (*)(syscall_context&)>,
                "sync syscall handlers must return i64 from invoke(syscall_context&)");
            completion.complete(Impl::invoke(context));
        }

        static constexpr syscall_descriptor descriptor{Id, &thunk};
    };

    template <typename Impl, syscall_id Id>
    struct syscall_handler<Impl, Id, syscall_mode::async> {
        using syscall_descriptor_marker = void;

        static constexpr syscall_id id = Id;
        static void thunk(syscall_context& context, core::completion<i64>& completion) {
            static_assert(std::is_same_v<decltype(&Impl::invoke), task_syscall_handler>,
                          "async syscall handlers must return core::task<i64> from "
                          "invoke(syscall_context&)");
            auto operation = Impl::invoke(context);
            if (!operation.valid()) {
                completion.fail(operation.error());
                return;
            }
            operation.resume();
            if (!operation.done()) {
                completion.cancel();
                return;
            }
            if (operation.failed()) {
                completion.fail(operation.error());
                return;
            }
            completion.complete(*operation.value());
        }

        static constexpr syscall_descriptor descriptor{Id, &thunk};
    };

    template <typename Impl, syscall_id Id>
    struct handler {
        using syscall_descriptor_marker = void;

        static constexpr syscall_id id = Id;

        static void thunk(syscall_context& context, core::completion<i64>& completion) {
            if constexpr (detail::is_async_handler<decltype(&Impl::invoke)>::value) {
                Impl::invoke(context, completion);
            }
            else {
                static_assert(
                    std::is_same_v<decltype(&Impl::invoke), task_syscall_handler>,
                    "syscall handlers must return core::task<i64> or accept "
                    "context and completion");
                auto operation = Impl::invoke(context);
                if (!operation.valid()) {
                    completion.fail(operation.error());
                    return;
                }
                operation.resume();
                if (!operation.done()) {
                    completion.cancel();
                    return;
                }
                if (operation.failed()) {
                    completion.fail(operation.error());
                    return;
                }
                completion.complete(*operation.value());
            }
        }

        static constexpr syscall_descriptor descriptor{Id, &thunk};
    };

    namespace detail {

        template <typename... Descriptors>
        consteval bool valid_descriptor_ids() {
            constexpr syscall_id
                ids[sizeof...(Descriptors) == 0 ? 1 : sizeof...(Descriptors)] = {
                    Descriptors::id...};
            for (u32 i = 0; i < sizeof...(Descriptors); ++i) {
                if (static_cast<u32>(ids[i]) >= syscall_table_capacity) {
                    return false;
                }
                for (u32 j = i + 1; j < sizeof...(Descriptors); ++j) {
                    if (ids[i] == ids[j]) {
                        return false;
                    }
                }
            }
            return true;
        }

        template <typename Descriptor, typename = void>
        struct valid_descriptor_interface {
            static constexpr bool value = false;
        };

        template <typename Descriptor>
        struct valid_descriptor_interface<
            Descriptor,
            decltype((void) typename Descriptor::syscall_descriptor_marker{},
                     (void) Descriptor::descriptor, (void) Descriptor::descriptor.invoke,
                     (void) &Descriptor::invoke)> {
            static constexpr bool value =
                is_async_handler<decltype(&Descriptor::invoke)>::value ||
                std::is_same_v<decltype(&Descriptor::invoke), task_syscall_handler> ||
                std::is_same_v<decltype(&Descriptor::invoke), i64 (*)(syscall_context&)>;
        };

    } // namespace detail

    namespace detail {

        template <typename Descriptor>
        bool dispatch_descriptor(syscall_id id, syscall_context& context,
                                 core::completion<i64>& completion) {
            if (Descriptor::descriptor.id != id)
                return false;
            Descriptor::descriptor.invoke(context, completion);
            return true;
        }

        template <typename... Descriptors>
        bool dispatch_descriptors(syscall_id id, syscall_context& context,
                                  core::completion<i64>& completion) {
            bool dispatched = false;
            ((dispatched = dispatched ||
                           dispatch_descriptor<Descriptors>(id, context, completion)),
             ...);
            return dispatched;
        }

    } // namespace detail

    template <typename... Descriptors>
    bool dispatch_async(syscall_id id, syscall_context& context,
                        core::completion<i64>& completion) {
        static_assert(
            detail::valid_descriptor_ids<Descriptors...>(),
            "syscall descriptor IDs must be unique and within ABI table capacity");
        static_assert((detail::valid_descriptor_interface<Descriptors>::value && ...),
                      "syscall descriptors must expose a CRTP handler interface");
        if (detail::dispatch_descriptors<Descriptors...>(id, context, completion))
            return true;
        completion.complete(-ENOSYS);
        return false;
    }

    /// Dispatch a system call: the number the caller trapped with, the six
    /// registers it passed, and the image the entry packed them into.  The
    /// registers travel as registers, as they did into the entry; the
    /// image travels for the one handler that reads it.
    i64 dispatch(syscall_id id, i64 a0, i64 a1, i64 a2, i64 a3, i64 a4, i64 a5,
                 const arch::syscall_frame& frame);

    bool production_dispatch(syscall_context& context, i64& result);
    i64  legacy_dispatch(const syscall_context& context);

} // namespace astia::sys
