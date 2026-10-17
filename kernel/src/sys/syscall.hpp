#pragma once

#include <astia/common.hpp>

namespace astia::sys {

    /// Architecture-independent syscall numbers mirror the public ABI.
    enum class syscall_id : u32 {
        io_setup = 0,
        io_destroy,
        io_enter,
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
    };

    /// How many registers the ABI hands every trap — and so the most
    /// arguments any handler can be given.
    inline constexpr u32 syscall_argument_count = 6;

    /// The ABI's own shape: six machine words.  This is what the table
    /// stores and the arch layer calls; handlers do not spell it out —
    /// register_syscall() derives it from the handler (below).
    using syscall_handler = i64 (*)(i64 arg0, i64 arg1, i64 arg2, i64 arg3, i64 arg4,
                                    i64 arg5);

    namespace detail {

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

        /// One handler widened to the ABI's six-register form.  Parameter
        /// n of the handler is register n, cast to the type the handler
        /// declared it with: a `u32 flags` truncates a junk-high register
        /// exactly as the per-handler casts used to, except the cast is
        /// named once here and not repeated in every body.
        template <auto Fn, typename Signature, typename Index>
        struct adapter;

        template <auto Fn, typename R, typename... A, u32... I>
        struct adapter<Fn, R (*)(A...), index_sequence<I...>> {
            /// A handler of arity N leaves registers N..5 unused here; that
            /// is the whole point of the adapter, so they are marked as
            /// such rather than named into a body.
            static i64 call([[maybe_unused]] i64 a0, [[maybe_unused]] i64 a1,
                            [[maybe_unused]] i64 a2, [[maybe_unused]] i64 a3,
                            [[maybe_unused]] i64 a4, [[maybe_unused]] i64 a5) {
                return static_cast<i64>(
                    Fn(static_cast<A>(argument<I>(a0, a1, a2, a3, a4, a5))...));
            }
        };

        template <auto Fn>
        using widened =
            adapter<Fn, decltype(Fn),
                    typename make_index_sequence<arity<decltype(Fn)>::value>::type>;

        /// The table door, for a handler already in the six-register form.
        /// register_syscall() is what callers use; this is what it hands
        /// the table.
        bool register_abi(syscall_id id, syscall_handler handler);

    } // namespace detail

    /// Register a handler written with only the arguments it uses, in ABI
    /// order and of the types it means:
    ///
    ///     i64 sys_waitpid(i32 pid, u64 status_ptr);
    ///     i64 sys_getpid();
    ///
    /// The handler is a template argument (the id is not), so the adapter
    /// is a distinct function per handler and the table keeps storing
    /// plain function pointers.  A handler that asks for more than the six
    /// ABI registers, or that is not a function at all, is a compile error
    /// here rather than a bad call at the first trap.
    template <auto handler>
    bool register_syscall(syscall_id id) {
        static_assert(detail::arity<decltype(handler)>::value <= syscall_argument_count,
                      "a syscall handler sees the six ABI registers: give it at most six "
                      "parameters, in the order the ABI passes them");
        return detail::register_abi(id, &detail::widened<handler>::call);
    }

    /// Dispatch a system call
    i64 dispatch(syscall_id id, i64 a0, i64 a1, i64 a2, i64 a3, i64 a4, i64 a5);

} // namespace astia::sys
