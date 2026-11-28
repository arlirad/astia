#include <astia/api/errno.h>
#include <astia/dev/driver.hpp>

#include "support/task_host.hpp"
#include "support/test.hpp"

#include <atomic>
#include <barrier>
#include <coroutine>
#include <thread>

namespace {
    class test_info final : public astia::hw::device_info {
        public:
        test_info(astia::u32 value, int* destroyed = nullptr)
            : value_(value), destroyed_(destroyed) {}

        ~test_info() {
            if (destroyed_ != nullptr)
                ++*destroyed_;
        }

        astia::hw::bus_kind bus() const override {
            return astia::hw::bus_kind::unknown;
        }

        bool equals(const astia::hw::device_info& other) const override {
            const auto* value = static_cast<const test_info*>(&other);
            return value_ == value->value_;
        }

        astia::u32 value() const {
            return value_;
        }

        private:
        astia::u32 value_;
        int*       destroyed_;
    };

    astia::mm::shptr<astia::hw::device_info>
    device_candidate(astia::mm::shptr<test_info>&& value) {
        return astia::mm::shptr<astia::hw::device_info>(
            static_cast<astia::mm::shptr<test_info>&&>(value));
    }

    astia::mm::shptr<astia::hw::device_info> typed_device(astia::u32 value) {
        astia::mm::shptr<test_info> info(new test_info(value));

        return device_candidate(static_cast<astia::mm::shptr<test_info>&&>(info));
    }

    class test_product final : public astia::hw::product {
        public:
        explicit test_product(int* destroyed) : destroyed_(destroyed) {}

        ~test_product() {
            ++*destroyed_;
        }

        private:
        int* destroyed_;
    };

    struct suspend_gate {
        std::atomic<std::coroutine_handle<>> parked{};

        bool await_ready() const noexcept {
            return false;
        }

        void await_suspend(std::coroutine_handle<> handle) noexcept {
            parked.store(handle, std::memory_order_release);
        }

        void await_resume() const noexcept {}
    };

    struct typed_state {
        astia::u32     match_value        = 0;
        astia::i64     probe_result       = 0;
        astia::i64     start_result       = 0;
        bool           publish_product    = false;
        int            match_calls        = 0;
        int            probe_calls        = 0;
        int            start_calls        = 0;
        int            stop_calls         = 0;
        int            remove_calls       = 0;
        int            publish_calls      = 0;
        int            destroyed_products = 0;
        astia::error_t remove_error       = astia::error_t::enone;
        suspend_gate*  remove_gate        = nullptr;
    };

    class test_driver final : public astia::hw::driver {
        public:
        test_driver(typed_state& state, const char* name = "typed-test")
            : state_(state), name_(name) {}

        const char* name() const override {
            return name_;
        }

        bool match(const astia::hw::device_info& info) const override {
            ++state_.match_calls;
            const auto& value = static_cast<const test_info&>(info);
            return state_.match_value == 0 || state_.match_value == value.value();
        }

        astia::core::task<astia::i64> probe(const astia::hw::device_info&) override {
            ++state_.probe_calls;
            co_return state_.probe_result;
        }

        astia::core::task<astia::i64> start(const astia::hw::device_info&) override {
            ++state_.start_calls;
            co_return state_.start_result;
        }

        astia::core::task<void> stop(const astia::hw::device_info&) override {
            ++state_.stop_calls;
            co_return astia::error_t::enone;
        }

        astia::core::task<void> remove(const astia::hw::device_info&) override {
            ++state_.remove_calls;

            if (state_.remove_gate != nullptr)
                co_await *state_.remove_gate;

            co_return state_.remove_error;
        }

        astia::hw::product_handle publish(const astia::hw::device_info&) override {
            ++state_.publish_calls;
            if (!state_.publish_product)
                return {};
            return astia::hw::product_handle(
                new test_product(&state_.destroyed_products));
        }

        astia::core::task<bool> selftest() override {
            co_return true;
        }

        private:
        typed_state& state_;
        const char*  name_;
    };

    struct suspend_forever {
        bool await_ready() const noexcept {
            return false;
        }

        void await_suspend(std::coroutine_handle<>) const noexcept {}

        void await_resume() const noexcept {}
    };

    class suspended_driver final : public astia::hw::driver {
        public:
        const char* name() const override {
            return "suspended";
        }

        bool match(const astia::hw::device_info&) const override {
            return true;
        }

        astia::core::task<astia::i64> probe(const astia::hw::device_info&) override {
            co_await suspend_forever{};
            co_return 0;
        }

        astia::core::task<astia::i64> start(const astia::hw::device_info&) override {
            co_return 0;
        }

        astia::core::task<void> stop(const astia::hw::device_info&) override {
            co_return astia::error_t::enone;
        }

        astia::core::task<void> remove(const astia::hw::device_info&) override {
            co_return astia::error_t::enone;
        }

        astia::core::task<bool> selftest() override {
            co_await suspend_forever{};
            co_return true;
        }
    };

    astia::i64 bind_sync(astia::hw::driver_registry&              manager,
                         astia::mm::shptr<astia::hw::device_info> device,
                         astia::hw::device_handle&                handle) {
        astia::i64 result = -1;

        ASTIA_CHECK(astia_test::run_task(
            manager.bind_device(
                static_cast<astia::mm::shptr<astia::hw::device_info>&&>(device), handle),
            result));

        return result;
    }

    astia::i64 unbind_sync(astia::hw::driver_registry& manager,
                           astia::hw::device_handle    handle) {
        astia::i64 result = -1;

        ASTIA_CHECK(astia_test::run_task(manager.unbind(handle), result));

        return result;
    }

    astia::i64 unbind_owner_sync(astia::hw::driver_registry& manager,
                                 astia::hw::owner_id         owner) {
        astia::i64 result = -1;

        ASTIA_CHECK(astia_test::run_task(manager.unbind_owner(owner), result));

        return result;
    }

    astia::i64 unbind_all_sync(astia::hw::driver_registry& manager) {
        astia::i64 result = -1;

        ASTIA_CHECK(astia_test::run_task(manager.unbind_all(), result));

        return result;
    }

    /// Unbind `handle` while its remove() is parked, then run `loser` on a
    /// second thread against the same binding.  The winner's teardown resumes
    /// only after the loser has completed, so the interleaving is fixed.
    template <typename Loser>
    astia::i64 race_unbind(astia::hw::driver_registry& manager,
                           astia::hw::device_handle handle, suspend_gate& gate,
                           astia::i64& loser_result, Loser loser) {
        std::barrier parked{2};
        std::barrier released{2};
        astia::i64   winner_result = -1;

        std::thread winner([&] {
            auto task = manager.unbind(handle);

            task.resume();
            parked.arrive_and_wait();
            released.arrive_and_wait();

            auto parked = gate.parked.load(std::memory_order_acquire);
            if (parked)
                parked.resume();

            winner_result = *task.value();
        });

        std::thread rival([&] {
            parked.arrive_and_wait();

            loser_result = loser();

            released.arrive_and_wait();
        });

        winner.join();
        rival.join();

        return winner_result;
    }

    astia::hw::owner_id resolved_owner() {
        return astia::hw::owner_id{9};
    }

    struct owner_resolver_scope {
        owner_resolver_scope() {
            astia::hw::set_owner_resolver(&resolved_owner);
        }

        ~owner_resolver_scope() {
            astia::hw::set_owner_resolver(nullptr);
        }
    };

    bool selftests_sync(astia::hw::driver_registry& manager) {
        bool result = false;

        ASTIA_CHECK(astia_test::run_task(manager.selftests(), result));

        return result;
    }
} // namespace

ASTIA_TEST(driver_manager_registers_driver_with_owner) {
    using namespace astia::hw;

    driver_registry manager;
    typed_state     state;
    test_driver     driver(state);

    state.match_value = 42;

    driver_handle registered{};

    ASTIA_CHECK(manager.register_driver(driver, registered, owner_id{7},
                                        driver_resource_irq) == 0);
    ASTIA_CHECK(registered.valid());
    ASTIA_CHECK(manager.valid(registered));
    ASTIA_CHECK(manager.driver_count() == 1);

    device_handle bound{};

    ASTIA_CHECK(bind_sync(manager, typed_device(42), bound) == 0);
    ASTIA_CHECK(bound.valid());
    ASTIA_CHECK(manager.valid(bound));
    ASTIA_CHECK(manager.device_count() == 1);
    ASTIA_CHECK(state.match_calls == 1);
    ASTIA_CHECK(state.probe_calls == 1);
    ASTIA_CHECK(state.start_calls == 1);
    ASTIA_CHECK(manager.owner_of(bound) == owner_id{7});
    ASTIA_CHECK(manager.devices_owned_by(owner_id{7}) == 1);
    ASTIA_CHECK(selftests_sync(manager));

    driver_handle from_binding{};

    ASTIA_CHECK(manager.driver_of(bound, from_binding));
    ASTIA_CHECK(from_binding == registered);

    ASTIA_CHECK(unbind_sync(manager, bound) == 0);
    ASTIA_CHECK(state.stop_calls == 1);
    ASTIA_CHECK(state.remove_calls == 1);
    ASTIA_CHECK(!manager.valid(bound));
    ASTIA_CHECK(unbind_sync(manager, bound) == -ENOENT);

    ASTIA_CHECK(manager.unregister_driver(registered) == 0);
    ASTIA_CHECK(!manager.valid(registered));
    ASTIA_CHECK(manager.unregister_driver(registered) == -ENOENT);
    ASTIA_CHECK(manager.driver_count() == 0);
}

ASTIA_TEST(driver_manager_rejects_invalid_or_duplicate_registration) {
    using namespace astia::hw;

    driver_registry manager;
    typed_state     state;
    test_driver     driver(state);
    driver_handle   handle{};

    ASTIA_CHECK(manager.register_driver(driver, handle, kernel_owner, 0) == -EINVAL);
    ASTIA_CHECK(!handle.valid());
    ASTIA_CHECK(manager.register_driver(driver, handle, kernel_owner, (1u << 31)) ==
                -EINVAL);
    ASTIA_CHECK(!handle.valid());
    ASTIA_CHECK(manager.register_driver(driver, handle) == 0);

    driver_handle duplicate{};

    ASTIA_CHECK(manager.register_driver(driver, duplicate) == -EEXIST);
    ASTIA_CHECK(manager.driver_count() == 1);
}

ASTIA_TEST(driver_manager_rolls_back_failed_start) {
    using namespace astia::hw;

    driver_registry manager;
    typed_state     state;
    test_driver     driver(state);

    state.start_result = -ENOMEM;

    driver_handle registered{};

    ASTIA_CHECK(manager.register_driver(driver, registered) == 0);

    device_handle bound{};

    ASTIA_CHECK(bind_sync(manager, typed_device(3), bound) == -ENOMEM);
    ASTIA_CHECK(state.probe_calls == 1);
    ASTIA_CHECK(state.start_calls == 1);
    ASTIA_CHECK(state.stop_calls == 1);
    ASTIA_CHECK(state.remove_calls == 1);
    ASTIA_CHECK(!bound.valid());
    ASTIA_CHECK(manager.device_count() == 0);
}

ASTIA_TEST(driver_manager_probe_failure_leaves_rollback_to_driver) {
    using namespace astia::hw;

    driver_registry manager;
    typed_state     state;
    test_driver     driver(state);

    state.probe_result = -EIO;

    driver_handle registered{};

    ASTIA_CHECK(manager.register_driver(driver, registered) == 0);

    device_handle bound{};

    ASTIA_CHECK(bind_sync(manager, typed_device(4), bound) == -EIO);
    ASTIA_CHECK(state.probe_calls == 1);
    ASTIA_CHECK(state.start_calls == 0);
    ASTIA_CHECK(state.remove_calls == 0);
    ASTIA_CHECK(manager.device_count() == 0);
}

ASTIA_TEST(driver_manager_rejects_duplicate_device) {
    using namespace astia::hw;

    driver_registry manager;
    typed_state     state;
    test_driver     driver(state);

    driver_handle registered{};

    ASTIA_CHECK(manager.register_driver(driver, registered) == 0);

    device_handle first{};

    ASTIA_CHECK(bind_sync(manager, typed_device(5), first) == 0);

    device_handle second{};

    ASTIA_CHECK(bind_sync(manager, typed_device(5), second) == -EEXIST);
    ASTIA_CHECK(manager.device_count() == 1);
    ASTIA_CHECK(unbind_sync(manager, first) == 0);
}

ASTIA_TEST(driver_manager_unbind_owner_releases_only_that_owner) {
    using namespace astia::hw;

    driver_registry manager;
    typed_state     first_state;
    typed_state     second_state;
    test_driver     first_driver(first_state, "first");
    test_driver     second_driver(second_state, "second");

    first_state.match_value  = 100;
    second_state.match_value = 200;

    driver_handle first_driver_handle{};
    driver_handle second_driver_handle{};

    ASTIA_CHECK(manager.register_driver(first_driver, first_driver_handle, owner_id{1}) ==
                0);
    ASTIA_CHECK(
        manager.register_driver(second_driver, second_driver_handle, owner_id{2}) == 0);

    device_handle first_device{};
    device_handle second_device{};

    ASTIA_CHECK(bind_sync(manager, typed_device(100), first_device) == 0);
    ASTIA_CHECK(bind_sync(manager, typed_device(200), second_device) == 0);
    ASTIA_CHECK(manager.devices_owned_by(owner_id{1}) == 1);
    ASTIA_CHECK(manager.devices_owned_by(owner_id{2}) == 1);

    ASTIA_CHECK(unbind_owner_sync(manager, owner_id{1}) == 1);
    ASTIA_CHECK(first_state.remove_calls == 1);
    ASTIA_CHECK(second_state.remove_calls == 0);
    ASTIA_CHECK(!manager.valid(first_device));
    ASTIA_CHECK(manager.valid(second_device));
    ASTIA_CHECK(manager.device_count() == 1);

    ASTIA_CHECK(unbind_all_sync(manager) == 1);
    ASTIA_CHECK(second_state.remove_calls == 1);
    ASTIA_CHECK(manager.device_count() == 0);
}

ASTIA_TEST(driver_manager_keeps_binding_when_remove_is_refused) {
    using namespace astia::hw;

    driver_registry manager;
    typed_state     state;
    test_driver     driver(state);

    driver_handle registered{};

    ASTIA_CHECK(manager.register_driver(driver, registered) == 0);

    device_handle bound{};

    ASTIA_CHECK(bind_sync(manager, typed_device(8), bound) == 0);

    state.remove_error = astia::error_t::ebusy;

    ASTIA_CHECK(unbind_sync(manager, bound) == -EBUSY);
    ASTIA_CHECK(state.remove_calls == 1);
    ASTIA_CHECK(manager.valid(bound));
    ASTIA_CHECK(manager.device_count() == 1);

    state.remove_error = astia::error_t::enone;

    ASTIA_CHECK(unbind_sync(manager, bound) == 0);
    ASTIA_CHECK(state.remove_calls == 2);
    ASTIA_CHECK(!manager.valid(bound));
    ASTIA_CHECK(manager.device_count() == 0);
}

ASTIA_TEST(driver_manager_concurrent_unbind_runs_teardown_once) {
    using namespace astia::hw;

    driver_registry manager;
    typed_state     state;
    test_driver     driver(state);

    driver_handle registered{};
    device_handle bound{};

    ASTIA_CHECK(manager.register_driver(driver, registered) == 0);
    ASTIA_CHECK(bind_sync(manager, typed_device(12), bound) == 0);

    suspend_gate gate;

    state.remove_gate = &gate;

    astia::i64       loser  = -1;
    const astia::i64 winner = race_unbind(manager, bound, gate, loser,
                                          [&] { return unbind_sync(manager, bound); });

    ASTIA_CHECK(winner == 0);
    ASTIA_CHECK(loser == -EBUSY);
    ASTIA_CHECK(state.stop_calls == 1);
    ASTIA_CHECK(state.remove_calls == 1);
    ASTIA_CHECK(!manager.valid(bound));
    ASTIA_CHECK(manager.device_count() == 0);
}

ASTIA_TEST(driver_manager_concurrent_refused_unbind_keeps_binding_for_retry) {
    using namespace astia::hw;

    driver_registry manager;
    typed_state     state;
    test_driver     driver(state);

    driver_handle registered{};
    device_handle bound{};

    ASTIA_CHECK(manager.register_driver(driver, registered) == 0);
    ASTIA_CHECK(bind_sync(manager, typed_device(13), bound) == 0);

    suspend_gate gate;

    state.remove_error = astia::error_t::ebusy;
    state.remove_gate  = &gate;

    astia::i64       loser  = 0;
    const astia::i64 winner = race_unbind(manager, bound, gate, loser,
                                          [&] { return unbind_sync(manager, bound); });

    ASTIA_CHECK(winner == -EBUSY);
    ASTIA_CHECK(loser == -EBUSY);
    ASTIA_CHECK(state.stop_calls == 1);
    ASTIA_CHECK(state.remove_calls == 1);
    ASTIA_CHECK(manager.valid(bound));
    ASTIA_CHECK(manager.device_count() == 1);

    state.remove_error = astia::error_t::enone;
    state.remove_gate  = nullptr;

    ASTIA_CHECK(unbind_sync(manager, bound) == 0);
    ASTIA_CHECK(state.remove_calls == 2);
    ASTIA_CHECK(!manager.valid(bound));
    ASTIA_CHECK(manager.device_count() == 0);
}

ASTIA_TEST(driver_manager_unbind_owner_skips_binding_being_detached) {
    using namespace astia::hw;

    driver_registry manager;
    typed_state     state;
    test_driver     driver(state);

    driver_handle registered{};
    device_handle bound{};

    ASTIA_CHECK(manager.register_driver(driver, registered, owner_id{4}) == 0);
    ASTIA_CHECK(bind_sync(manager, typed_device(14), bound) == 0);

    suspend_gate gate;

    state.remove_gate = &gate;

    astia::i64       loser  = -1;
    const astia::i64 winner = race_unbind(manager, bound, gate, loser, [&] {
        return unbind_owner_sync(manager, owner_id{4});
    });

    ASTIA_CHECK(winner == 0);
    ASTIA_CHECK(loser == 0);
    ASTIA_CHECK(state.stop_calls == 1);
    ASTIA_CHECK(state.remove_calls == 1);
    ASTIA_CHECK(manager.devices_owned_by(owner_id{4}) == 0);
    ASTIA_CHECK(manager.device_count() == 0);
}

ASTIA_TEST(driver_manager_refuses_unregister_while_bound) {
    using namespace astia::hw;

    driver_registry manager;
    typed_state     state;
    test_driver     driver(state);

    driver_handle registered{};

    ASTIA_CHECK(manager.register_driver(driver, registered) == 0);

    device_handle bound{};

    ASTIA_CHECK(bind_sync(manager, typed_device(6), bound) == 0);
    ASTIA_CHECK(manager.unregister_driver(registered) == -EBUSY);
    ASTIA_CHECK(unbind_sync(manager, bound) == 0);
    ASTIA_CHECK(manager.unregister_driver(registered) == 0);
}

ASTIA_TEST(driver_manager_owns_published_product_until_unbind) {
    using namespace astia::hw;

    driver_registry manager;
    typed_state     state;
    test_driver     driver(state);

    state.publish_product = true;

    driver_handle registered{};

    ASTIA_CHECK(manager.register_driver(driver, registered) == 0);

    device_handle bound{};

    ASTIA_CHECK(bind_sync(manager, typed_device(7), bound) == 0);
    ASTIA_CHECK(state.destroyed_products == 0);
    ASTIA_CHECK(unbind_sync(manager, bound) == 0);
    ASTIA_CHECK(state.destroyed_products == 1);
}

ASTIA_TEST(driver_manager_shared_device_lifetime) {
    using namespace astia::hw;

    int destroyed = 0;

    driver_registry manager;
    typed_state     state;
    test_driver     driver(state);

    astia::mm::shptr<test_info> info(new test_info(9, &destroyed));

    ASTIA_CHECK(info);

    astia::mm::shptr<test_info> external = info;

    driver_handle registered{};

    ASTIA_CHECK(manager.register_driver(driver, registered) == 0);

    device_handle bound{};

    ASTIA_CHECK(
        bind_sync(manager,
                  device_candidate(static_cast<astia::mm::shptr<test_info>&&>(info)),
                  bound) == 0);
    ASTIA_CHECK(!info);
    ASTIA_CHECK(external);
    ASTIA_CHECK(unbind_sync(manager, bound) == 0);
    ASTIA_CHECK(destroyed == 0);

    external = {};

    ASTIA_CHECK(destroyed == 1);
}

ASTIA_TEST(driver_manager_sync_adapter_rejects_a_suspension) {
    using namespace astia::hw;

    driver_registry  manager;
    suspended_driver driver;
    driver_handle    registered{};

    ASTIA_CHECK(manager.register_driver(driver, registered) == 0);

    device_handle bound{};
    auto          task = manager.bind_device(typed_device(8), bound);

    ASTIA_CHECK(task.valid());
    task.resume();
    ASTIA_CHECK(!task.done());
    ASTIA_CHECK(!bound.valid());
    ASTIA_CHECK(manager.device_count() == 0);
}

ASTIA_TEST(driver_manager_selftest_is_task_shaped) {
    using namespace astia::hw;

    driver_registry  manager;
    suspended_driver driver;
    driver_handle    registered{};

    ASTIA_CHECK(manager.register_driver(driver, registered) == 0);

    auto task = manager.selftests();

    ASTIA_CHECK(task.valid());
    task.resume();
    ASTIA_CHECK(!task.done());
}

ASTIA_TEST(driver_manager_resolves_current_owner_for_kernel_requests) {
    using namespace astia::hw;

    driver_registry manager;
    typed_state     state;
    test_driver     driver(state);

    ASTIA_CHECK(current_owner() == kernel_owner);

    driver_handle registered{};

    {
        owner_resolver_scope resolver;

        ASTIA_CHECK(current_owner() == owner_id{9});
        ASTIA_CHECK(manager.register_driver(driver, registered) == 0);

        device_handle bound{};

        ASTIA_CHECK(bind_sync(manager, typed_device(11), bound) == 0);
        ASTIA_CHECK(manager.owner_of(bound) == owner_id{9});
        ASTIA_CHECK(unbind_sync(manager, bound) == 0);
    }

    ASTIA_CHECK(current_owner() == kernel_owner);
    ASTIA_CHECK(manager.unregister_driver(registered) == 0);
}
