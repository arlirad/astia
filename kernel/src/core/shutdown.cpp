#include <astia/atomic.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/log.hpp>
#include <astia/shutdown.hpp>

namespace astia::shutdown {
    namespace {
        using logs = astia::log::context<"shutdown">;

        constexpr usz max_handlers = detail::handler_capacity;

        struct registration {
            const char* name    = nullptr;
            handler     fn      = nullptr;
            void*       context = nullptr;
            tier        level   = tier::prepare;
        };

        registration          registrations[max_handlers]{};
        usz                   registration_count = 0;
        astia::atomic<u8>     state{0}; // 0 idle, 1 running, 2 complete
        astia::core::spinlock registration_lock;

        astia::atomic<const detail::dispatch_executor*> executor{nullptr};

        const char* tier_name(tier level) {
            switch (level) {
            case tier::prepare:
                return "prepare";
            case tier::flush:
                return "flush";
            case tier::quiesce:
                return "quiesce";
            case tier::platform:
                return "platform";
            case tier::final:
                return "final";
            case tier::count:
                break;
            }

            return "invalid";
        }

        /// Bounded dispatch: `poll` is the only way a handler reports back, so
        /// neither a handler nor a missing clock can hold the loop forever.
        bool invoke_bounded(const detail::dispatch_executor& backend,
                            const registration& entry, bool& timed_out) {
            timed_out = false;

            void* task = nullptr;

            if (!backend.start(entry.fn, entry.context, &task) || task == nullptr)
                return entry.fn(entry.context);

            u64 started = 0;
            u32 polls   = 0;

            while (true) {
                const int status = backend.poll(task);
                if (status == 1)
                    return true;

                if (status == 2)
                    return false;

                if (polls >= handler_max_polls) {
                    backend.abandon(task);

                    timed_out = true;

                    return false;
                }

                ++polls;

                const u64 now = backend.now_ns();
                if (now != 0) {
                    if (started == 0) {
                        started = now;
                    }
                    else if (now >= started && now - started >= handler_timeout_ns) {
                        backend.abandon(task);

                        timed_out = true;

                        return false;
                    }
                }

                backend.relax();
            }
        }
    } // namespace

    bool register_handler(tier level, const char* name, handler fn, void* context) {
        if (level >= tier::count || name == nullptr || fn == nullptr)
            return false;

        astia::core::spinlock_guard guard(registration_lock);

        if (state.load() != 0 || registration_count == max_handlers)
            return false;

        if (level == tier::final) {
            for (usz i = 0; i < registration_count; ++i) {
                if (registrations[i].level == tier::final)
                    return false;
            }
        }

        registrations[registration_count++] = {
            name,
            fn,
            context,
            level,
        };

        return true;
    }

    namespace detail {
        bool set_dispatch_executor(const dispatch_executor* value) {
            if (value != nullptr && (value->start == nullptr || value->poll == nullptr ||
                                     value->abandon == nullptr ||
                                     value->relax == nullptr || value->now_ns == nullptr))
                return false;

            astia::core::spinlock_guard guard(registration_lock);

            if (state.load() != 0)
                return false;

            executor.store(value);

            return true;
        }
    } // namespace detail

    bool initiate(handler final_override, void* final_context) {
        registration snapshot[max_handlers]{};

        usz count = 0;

        {
            astia::core::spinlock_guard guard(registration_lock);

            u8 expected = 0;

            if (!state.compare_exchange_strong(expected, 1)) {
                logs::warning("shutdown already requested");

                return false;
            }

            count = registration_count;

            for (usz i = 0; i < count; ++i) {
                snapshot[i] = registrations[i];
            }
        }

        const detail::dispatch_executor* active = executor.load();

        bool all_ok       = true;
        bool platform_ran = false;
        bool final_ran    = false;

        for (u8 level = 0; level < static_cast<u8>(tier::count); ++level) {
            const tier current = static_cast<tier>(level);

            if (current == tier::final && final_override != nullptr) {
                logs::info("tier=final handler=override");

                final_ran = true;

                if (!final_override(final_context)) {
                    logs::error("tier=final handler=override failed");

                    all_ok = false;
                }

                continue;
            }

            for (usz i = 0; i < count; ++i) {
                const registration& entry = snapshot[i];

                if (entry.level != current)
                    continue;

                logs::info("tier=%s handler=%s", tier_name(current), entry.name);

                bool ok        = false;
                bool timed_out = false;

                if (active != nullptr && current != tier::final)
                    ok = invoke_bounded(*active, entry, timed_out);
                else
                    ok = entry.fn(entry.context);

                if (current == tier::platform)
                    platform_ran = true;
                else if (current == tier::final)
                    final_ran = true;

                if (timed_out) {
                    logs::error("tier=%s handler=%s timed out", tier_name(current),
                                entry.name);

                    all_ok = false;
                }
                else if (!ok) {
                    logs::error("tier=%s handler=%s failed", tier_name(current),
                                entry.name);

                    all_ok = false;
                }
            }
        }

        if (!platform_ran) {
            logs::error("no platform shutdown handler registered");

            all_ok = false;
        }

        if (!final_ran) {
            logs::error("no final shutdown handler registered");

            all_ok = false;
        }

        state.store(2);

        return all_ok;
    }

#ifdef ASTIA_HOST_TEST
    namespace detail {
        void reset_for_test() {
            astia::core::spinlock_guard guard(registration_lock);

            registration_count = 0;

            for (registration& entry : registrations) {
                entry = registration{};
            }

            executor.store(nullptr);
            state.store(0);
        }
    } // namespace detail
#endif
} // namespace astia::shutdown
