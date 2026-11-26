#include "support/test.hpp"
#ifdef RENAME_NOREPLACE
#undef RENAME_NOREPLACE
#endif
#ifdef RENAME_EXCHANGE
#undef RENAME_EXCHANGE
#endif
#ifdef RENAME_WHITEOUT
#undef RENAME_WHITEOUT
#endif
#include <astia/api/errno.h>
#include <astia/dev/driver.hpp>

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

    class test_driver : public astia::hw::driver {
        public:
        test_driver(bool should_match, bool should_probe)
            : should_match_(should_match), should_probe_(should_probe) {}

        const char* name() const override {
            return "driver-test";
        }

        bool match(const astia::hw::device_info& info) const override {
            const auto& value = static_cast<const test_info&>(info);
            matched_value_    = value.value();
            ++match_count_;
            return should_match_;
        }

        bool probe(const astia::hw::device_info& info) override {
            const auto& value = static_cast<const test_info&>(info);
            probed_value_     = value.value();
            ++probe_count_;
            return should_probe_;
        }

        void remove(const astia::hw::device_info& info) override {
            const auto& candidate = static_cast<const test_info&>(info);
            removed_value_        = candidate.value();
            ++remove_count_;
        }

        bool               should_match_;
        bool               should_probe_;
        mutable int        match_count_   = 0;
        int                probe_count_   = 0;
        int                remove_count_  = 0;
        mutable astia::u32 matched_value_ = 0;
        astia::u32         probed_value_  = 0;
        astia::u32         removed_value_ = 0;
    };

    class test_product final : public astia::hw::product {
        public:
        explicit test_product(int* destroyed) : destroyed_(destroyed) {}
        ~test_product() {
            ++*destroyed_;
        }

        private:
        int* destroyed_;
    };

}

namespace {

    struct typed_state {
        astia::u32 match_value        = 0;
        astia::i64 probe_result       = 0;
        astia::i64 start_result       = 0;
        bool       publish_product    = false;
        int        match_calls        = 0;
        int        probe_calls        = 0;
        int        start_calls        = 0;
        int        stop_calls         = 0;
        int        remove_calls       = 0;
        int        publish_calls      = 0;
        int        destroyed_products = 0;
    };

    bool typed_match(void* context, const astia::hw::device_info& info) {
        auto& state = *static_cast<typed_state*>(context);
        ++state.match_calls;
        const auto& value = static_cast<const test_info&>(info);
        return state.match_value == 0 || state.match_value == value.value();
    }

    astia::i64 typed_probe(void* context, const astia::hw::device_info&) {
        auto& state = *static_cast<typed_state*>(context);
        ++state.probe_calls;
        return state.probe_result;
    }

    astia::i64 typed_start(void* context, const astia::hw::device_info&) {
        auto& state = *static_cast<typed_state*>(context);
        ++state.start_calls;
        return state.start_result;
    }

    void typed_stop(void* context, const astia::hw::device_info&) {
        ++static_cast<typed_state*>(context)->stop_calls;
    }

    void typed_remove(void* context, const astia::hw::device_info&) {
        ++static_cast<typed_state*>(context)->remove_calls;
    }

    astia::hw::product_handle typed_publish(void* context,
                                            const astia::hw::device_info&) {
        auto& state = *static_cast<typed_state*>(context);
        ++state.publish_calls;
        if (!state.publish_product)
            return {};
        return astia::hw::product_handle(new test_product(&state.destroyed_products));
    }

    astia::hw::driver_descriptor
    typed_descriptor(typed_state& state, astia::hw::owner_id owner = {},
                     astia::u16 features  = 0,
                     astia::u32 resources = astia::hw::driver_resource_irq |
                                            astia::hw::driver_resource_mmio) {
        astia::hw::driver_descriptor descriptor{};
        descriptor.name               = "typed-test";
        descriptor.owner              = owner;
        descriptor.context            = &state;
        descriptor.features           = features;
        descriptor.resources          = resources;
        descriptor.operations.match   = typed_match;
        descriptor.operations.probe   = typed_probe;
        descriptor.operations.start   = typed_start;
        descriptor.operations.stop    = typed_stop;
        descriptor.operations.remove  = typed_remove;
        descriptor.operations.publish = typed_publish;
        return descriptor;
    }

    astia::mm::shptr<astia::hw::device_info> typed_device(astia::u32 value) {
        astia::mm::shptr<test_info> info(new test_info(value));
        return device_candidate(static_cast<astia::mm::shptr<test_info>&&>(info));
    }

} // namespace

ASTIA_TEST(driver_manager_passes_candidate_to_match_and_probe) {
    test_driver                 driver(true, true);
    astia::hw::driver_registry  manager;
    astia::mm::shptr<test_info> info(new test_info(42));
    ASTIA_CHECK(info);

    ASTIA_CHECK(manager.register_driver(driver));
    ASTIA_CHECK(
        manager.bind(device_candidate(static_cast<astia::mm::shptr<test_info>&&>(info))));
    ASTIA_CHECK(driver.match_count_ == 1);
    ASTIA_CHECK(driver.probe_count_ == 1);
    ASTIA_CHECK(driver.matched_value_ == 42);
    ASTIA_CHECK(driver.probed_value_ == 42);
    ASTIA_CHECK(manager.device_count() == 1);
    manager.unbind_all();
    ASTIA_CHECK(driver.remove_count_ == 1);
    ASTIA_CHECK(driver.removed_value_ == 42);
}

ASTIA_TEST(driver_manager_rejects_failed_probe) {
    int                         destroyed = 0;
    test_driver                 driver(true, false);
    astia::hw::driver_registry  manager;
    astia::mm::shptr<test_info> info(new test_info(7, &destroyed));
    ASTIA_CHECK(info);

    ASTIA_CHECK(manager.register_driver(driver));
    ASTIA_CHECK(!manager.bind(
        device_candidate(static_cast<astia::mm::shptr<test_info>&&>(info))));
    ASTIA_CHECK(driver.match_count_ == 1);
    ASTIA_CHECK(driver.probe_count_ == 1);
    ASTIA_CHECK(manager.device_count() == 0);
    ASTIA_CHECK(destroyed == 1);
}

ASTIA_TEST(driver_manager_shared_device_lifetime) {
    int                         destroyed = 0;
    test_driver                 driver(true, true);
    astia::hw::driver_registry  manager;
    astia::mm::shptr<test_info> info(new test_info(9, &destroyed));
    ASTIA_CHECK(info);

    ASTIA_CHECK(manager.register_driver(driver));
    auto candidate = info;
    ASTIA_CHECK(
        manager.bind(device_candidate(static_cast<astia::mm::shptr<test_info>&&>(info))));
    ASTIA_CHECK(!info);
    ASTIA_CHECK(candidate);
    manager.unbind_all();
    ASTIA_CHECK(driver.remove_count_ == 1);
    ASTIA_CHECK(destroyed == 0);
    candidate = {};
    ASTIA_CHECK(destroyed == 1);
}

ASTIA_TEST(driver_manager_rejects_equal_candidates) {
    test_driver                 driver(true, true);
    astia::hw::driver_registry  manager;
    astia::mm::shptr<test_info> first(new test_info(11));
    astia::mm::shptr<test_info> second(new test_info(11));

    ASTIA_CHECK(manager.register_driver(driver));
    ASTIA_CHECK(manager.bind(
        device_candidate(static_cast<astia::mm::shptr<test_info>&&>(first))));
    ASTIA_CHECK(!manager.bind(
        device_candidate(static_cast<astia::mm::shptr<test_info>&&>(second))));
    ASTIA_CHECK(manager.device_count() == 1);
    manager.unbind_all();
}

ASTIA_TEST(driver_manager_owns_published_product_until_unbind) {
    int                     destroyed = 0;
    class publishing_driver final : public test_driver {
        public:
        explicit publishing_driver(int* destroyed)
            : test_driver(true, true), destroyed_(destroyed) {}
        astia::hw::product_handle publish(const astia::hw::device_info&) override {
            return astia::hw::product_handle(new test_product(destroyed_));
        }

        private:
        int* destroyed_;
    } driver(&destroyed);
    astia::hw::driver_registry  manager;
    astia::mm::shptr<test_info> info(new test_info(12));
    ASTIA_CHECK(manager.register_driver(driver));
    ASTIA_CHECK(
        manager.bind(device_candidate(static_cast<astia::mm::shptr<test_info>&&>(info))));
    ASTIA_CHECK(destroyed == 0);
    manager.unbind_all();
    ASTIA_CHECK(destroyed == 1);
}

ASTIA_TEST(driver_manager_registers_typed_driver_with_owner) {
    using namespace astia::hw;

    driver_registry manager;
    typed_state     state;
    state.match_value = 42;

    driver_token registered{};
    ASTIA_CHECK(
        manager.register_driver(typed_descriptor(state, owner_id{7}), registered) == 0);
    ASTIA_CHECK(registered.valid());
    ASTIA_CHECK(manager.valid(registered));
    ASTIA_CHECK(manager.driver_count() == 1);
    ASTIA_CHECK(manager.descriptor(registered) != nullptr);
    ASTIA_CHECK(manager.descriptor(registered)->owner == owner_id{7});

    device_token bound{};
    ASTIA_CHECK(manager.bind_device(typed_device(42), bound) == 0);
    ASTIA_CHECK(bound.valid());
    ASTIA_CHECK(manager.valid(bound));
    ASTIA_CHECK(manager.device_count() == 1);
    ASTIA_CHECK(state.match_calls == 1);
    ASTIA_CHECK(state.probe_calls == 1);
    ASTIA_CHECK(state.start_calls == 1);
    ASTIA_CHECK(manager.owner_of(bound) == owner_id{7});
    ASTIA_CHECK(manager.devices_owned_by(owner_id{7}) == 1);

    driver_token from_binding{};
    ASTIA_CHECK(manager.driver_of(bound, from_binding));
    ASTIA_CHECK(from_binding == registered);

    ASTIA_CHECK(manager.unbind(bound) == 0);
    ASTIA_CHECK(state.stop_calls == 1);
    ASTIA_CHECK(state.remove_calls == 1);
    ASTIA_CHECK(!manager.valid(bound));
    ASTIA_CHECK(manager.unbind(bound) == -ENOENT);

    ASTIA_CHECK(manager.unregister_driver(registered) == 0);
    ASTIA_CHECK(!manager.valid(registered));
    ASTIA_CHECK(manager.unregister_driver(registered) == -ENOENT);
    ASTIA_CHECK(manager.driver_count() == 0);
}

ASTIA_TEST(driver_manager_rejects_invalid_typed_descriptors) {
    using namespace astia::hw;

    auto expect = [](astia::i64 result, driver_descriptor descriptor) {
        driver_registry manager;
        driver_token    token{};
        ASTIA_CHECK(manager.register_driver(descriptor, token) == result);
        ASTIA_CHECK(!token.valid());
        ASTIA_CHECK(manager.driver_count() == 0);
    };

    typed_state       state;
    driver_descriptor descriptor = typed_descriptor(state);

    descriptor.abi_version = 0;
    expect(-EPROTONOSUPPORT, descriptor);

    descriptor             = typed_descriptor(state);
    descriptor.abi_version = driver_abi_version + 1;
    expect(-EPROTONOSUPPORT, descriptor);

    descriptor      = typed_descriptor(state);
    descriptor.name = nullptr;
    expect(-EINVAL, descriptor);

    descriptor         = typed_descriptor(state);
    descriptor.context = nullptr;
    expect(-EINVAL, descriptor);

    descriptor           = typed_descriptor(state);
    descriptor.resources = 0;
    expect(-EINVAL, descriptor);

    descriptor           = typed_descriptor(state);
    descriptor.resources = (1u << 31);
    expect(-EINVAL, descriptor);

    descriptor          = typed_descriptor(state);
    descriptor.features = (1u << 15);
    expect(-EOPNOTSUPP, descriptor);

    descriptor                  = typed_descriptor(state);
    descriptor.operations.start = nullptr;
    expect(-EINVAL, descriptor);
}

ASTIA_TEST(driver_manager_rolls_back_failed_start) {
    using namespace astia::hw;

    driver_registry manager;
    typed_state     state;
    state.start_result = -ENOMEM;

    driver_token registered{};
    ASTIA_CHECK(manager.register_driver(typed_descriptor(state), registered) == 0);

    device_token bound{};
    ASTIA_CHECK(manager.bind_device(typed_device(3), bound) == -ENOMEM);
    ASTIA_CHECK(state.probe_calls == 1);
    ASTIA_CHECK(state.start_calls == 1);
    ASTIA_CHECK(state.stop_calls == 1);
    ASTIA_CHECK(state.remove_calls == 1);
    ASTIA_CHECK(!bound.valid());
    ASTIA_CHECK(manager.device_count() == 0);
}

ASTIA_TEST(driver_manager_probe_rollback_calls_remove) {
    using namespace astia::hw;

    {
        driver_registry manager;
        typed_state     state;
        state.probe_result = -EIO;

        driver_token registered{};
        ASTIA_CHECK(
            manager.register_driver(
                typed_descriptor(state, kernel_owner, driver_feature_probe_rollback),
                registered) == 0);
        device_token bound{};
        ASTIA_CHECK(manager.bind_device(typed_device(4), bound) == -EIO);
        ASTIA_CHECK(state.probe_calls == 1);
        ASTIA_CHECK(state.remove_calls == 1);
        ASTIA_CHECK(state.start_calls == 0);
        ASTIA_CHECK(manager.device_count() == 0);
    }

    {
        driver_registry manager;
        typed_state     state;
        state.probe_result = -EIO;

        driver_token registered{};
        ASTIA_CHECK(manager.register_driver(typed_descriptor(state), registered) == 0);
        device_token bound{};
        ASTIA_CHECK(manager.bind_device(typed_device(4), bound) == -EIO);
        ASTIA_CHECK(state.remove_calls == 0);
        ASTIA_CHECK(manager.device_count() == 0);
    }
}

ASTIA_TEST(driver_manager_rejects_duplicate_registration_and_device) {
    using namespace astia::hw;

    driver_registry manager;
    typed_state     state;

    driver_token registered{};
    ASTIA_CHECK(manager.register_driver(typed_descriptor(state), registered) == 0);
    driver_token duplicate{};
    ASTIA_CHECK(manager.register_driver(typed_descriptor(state), duplicate) == -EEXIST);
    ASTIA_CHECK(manager.driver_count() == 1);

    device_token first{};
    ASTIA_CHECK(manager.bind_device(typed_device(5), first) == 0);
    device_token second{};
    ASTIA_CHECK(manager.bind_device(typed_device(5), second) == -EEXIST);
    ASTIA_CHECK(manager.device_count() == 1);
    ASTIA_CHECK(manager.unbind(first) == 0);
}

ASTIA_TEST(driver_manager_unbind_owner_releases_only_that_owner) {
    using namespace astia::hw;

    driver_registry manager;
    typed_state     first_state;
    typed_state     second_state;
    first_state.match_value  = 100;
    second_state.match_value = 200;

    driver_token first_driver{};
    driver_token second_driver{};
    ASTIA_CHECK(manager.register_driver(typed_descriptor(first_state, owner_id{1}),
                                        first_driver) == 0);
    ASTIA_CHECK(manager.register_driver(typed_descriptor(second_state, owner_id{2}),
                                        second_driver) == 0);

    device_token first_device{};
    device_token second_device{};
    ASTIA_CHECK(manager.bind_device(typed_device(100), first_device) == 0);
    ASTIA_CHECK(manager.bind_device(typed_device(200), second_device) == 0);
    ASTIA_CHECK(manager.devices_owned_by(owner_id{1}) == 1);
    ASTIA_CHECK(manager.devices_owned_by(owner_id{2}) == 1);

    ASTIA_CHECK(manager.unbind_owner(owner_id{1}) == 1);
    ASTIA_CHECK(first_state.remove_calls == 1);
    ASTIA_CHECK(second_state.remove_calls == 0);
    ASTIA_CHECK(!manager.valid(first_device));
    ASTIA_CHECK(manager.valid(second_device));
    ASTIA_CHECK(manager.device_count() == 1);

    ASTIA_CHECK(manager.unbind_all() == 1);
    ASTIA_CHECK(second_state.remove_calls == 1);
    ASTIA_CHECK(manager.device_count() == 0);
}

ASTIA_TEST(driver_manager_refuses_unregister_while_bound) {
    using namespace astia::hw;

    driver_registry manager;
    typed_state     state;

    driver_token registered{};
    ASTIA_CHECK(manager.register_driver(typed_descriptor(state), registered) == 0);
    device_token bound{};
    ASTIA_CHECK(manager.bind_device(typed_device(6), bound) == 0);
    ASTIA_CHECK(manager.unregister_driver(registered) == -EBUSY);
    ASTIA_CHECK(manager.unbind(bound) == 0);
    ASTIA_CHECK(manager.unregister_driver(registered) == 0);
}
