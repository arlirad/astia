#include <astia/atomic.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/log.hpp>
#include <astia/shutdown.hpp>

namespace astia::shutdown {
    namespace {
        using logs = astia::log::context<"shutdown">;

        constexpr usize max_handlers = 32;

        struct registration {
            const char* name    = nullptr;
            handler     fn      = nullptr;
            void*       context = nullptr;
            tier        level   = tier::prepare;
        };

        registration          registrations[max_handlers]{};
        usize                 registration_count = 0;
        astia::atomic<u8>     state{0}; // 0 idle, 1 running, 2 complete
        astia::core::spinlock registration_lock;

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
    } // namespace

    bool register_handler(tier level, const char* name, handler fn, void* context) {
        if (level >= tier::count || name == nullptr || fn == nullptr)
            return false;

        astia::core::spinlock_guard guard(registration_lock);

        if (state.load() != 0 || registration_count == max_handlers)
            return false;

        if (level == tier::final) {
            for (usize i = 0; i < registration_count; ++i) {
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

    bool initiate() {
        registration snapshot[max_handlers]{};

        usize count = 0;

        {
            astia::core::spinlock_guard guard(registration_lock);

            u8 expected = 0;

            if (!state.compare_exchange_strong(expected, 1)) {
                logs::warning("shutdown already requested");

                return false;
            }

            count = registration_count;

            for (usize i = 0; i < count; ++i) {
                snapshot[i] = registrations[i];
            }
        }

        bool all_ok       = true;
        bool platform_ran = false;
        bool final_ran    = false;

        for (u8 level = 0; level < static_cast<u8>(tier::count); ++level) {
            const tier current = static_cast<tier>(level);

            for (usize i = 0; i < count; ++i) {
                const registration& entry = snapshot[i];

                if (entry.level != current)
                    continue;

                logs::info("tier=%s handler=%s", tier_name(current), entry.name);

                const bool ok = entry.fn(entry.context);

                if (current == tier::platform)
                    platform_ran = true;
                else if (current == tier::final)
                    final_ran = true;

                if (!ok) {
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

            state.store(0);
        }
    } // namespace detail
#endif
} // namespace astia::shutdown
