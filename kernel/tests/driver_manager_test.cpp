#include <astia/api/errno.h>
#include <astia/dev/driver.hpp>

#include "support/test.hpp"

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

    bool typed_selftest(void*) {
        return true;
    }

    astia::hw::driver_descriptor
    typed_descriptor(typed_state& state, astia::hw::owner_id owner = {},
                     astia::u32 resources = astia::hw::driver_resource_irq |
                                            astia::hw::driver_resource_mmio) {
        astia::hw::driver_descriptor descriptor{};
        descriptor.name                = "typed-test";
        descriptor.owner               = owner;
        descriptor.context             = &state;
        descriptor.resources           = resources;
        descriptor.operations.match    = typed_match;
        descriptor.operations.probe    = typed_probe;
        descriptor.operations.start    = typed_start;
        descriptor.operations.stop     = typed_stop;
        descriptor.operations.remove   = typed_remove;
        descriptor.operations.publish  = typed_publish;
        descriptor.operations.selftest = typed_selftest;
        return descriptor;
    }

} // namespace

ASTIA_TEST(driver_manager_registers_typed_driver_with_owner) {
    using namespace astia::hw;

    driver_registry manager;
    typed_state     state;
    state.match_value = 42;

    driver_handle registered{};
    ASTIA_CHECK(
        manager.register_driver(typed_descriptor(state, owner_id{7}), registered) == 0);
    ASTIA_CHECK(registered.valid());
    ASTIA_CHECK(manager.valid(registered));
    ASTIA_CHECK(manager.driver_count() == 1);
    ASTIA_CHECK(manager.descriptor(registered) != nullptr);
    ASTIA_CHECK(manager.descriptor(registered)->owner == owner_id{7});

    device_handle bound{};
    ASTIA_CHECK(manager.bind_device(typed_device(42), bound) == 0);
    ASTIA_CHECK(bound.valid());
    ASTIA_CHECK(manager.valid(bound));
    ASTIA_CHECK(manager.device_count() == 1);
    ASTIA_CHECK(state.match_calls == 1);
    ASTIA_CHECK(state.probe_calls == 1);
    ASTIA_CHECK(state.start_calls == 1);
    ASTIA_CHECK(manager.owner_of(bound) == owner_id{7});
    ASTIA_CHECK(manager.devices_owned_by(owner_id{7}) == 1);

    driver_handle from_binding{};
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
        driver_handle   handle{};
        ASTIA_CHECK(manager.register_driver(descriptor, handle) == result);
        ASTIA_CHECK(!handle.valid());
        ASTIA_CHECK(manager.driver_count() == 0);
    };

    typed_state       state;
    driver_descriptor descriptor = typed_descriptor(state);

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

    descriptor                  = typed_descriptor(state);
    descriptor.operations.start = nullptr;
    expect(-EINVAL, descriptor);

    descriptor                     = typed_descriptor(state);
    descriptor.operations.selftest = nullptr;
    driver_registry optional;
    driver_handle   optional_handle{};
    ASTIA_CHECK(optional.register_driver(descriptor, optional_handle) == 0);
    ASTIA_CHECK(optional_handle.valid());
}

ASTIA_TEST(driver_manager_rolls_back_failed_start) {
    using namespace astia::hw;

    driver_registry manager;
    typed_state     state;
    state.start_result = -ENOMEM;

    driver_handle registered{};
    ASTIA_CHECK(manager.register_driver(typed_descriptor(state), registered) == 0);

    device_handle bound{};
    ASTIA_CHECK(manager.bind_device(typed_device(3), bound) == -ENOMEM);
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
    state.probe_result = -EIO;

    driver_handle registered{};
    ASTIA_CHECK(manager.register_driver(typed_descriptor(state), registered) == 0);
    device_handle bound{};
    ASTIA_CHECK(manager.bind_device(typed_device(4), bound) == -EIO);
    ASTIA_CHECK(state.probe_calls == 1);
    ASTIA_CHECK(state.start_calls == 0);
    ASTIA_CHECK(state.remove_calls == 0);
    ASTIA_CHECK(manager.device_count() == 0);
}

ASTIA_TEST(driver_manager_rejects_duplicate_registration_and_device) {
    using namespace astia::hw;

    driver_registry manager;
    typed_state     state;

    driver_handle registered{};
    ASTIA_CHECK(manager.register_driver(typed_descriptor(state), registered) == 0);
    driver_handle duplicate{};
    ASTIA_CHECK(manager.register_driver(typed_descriptor(state), duplicate) == -EEXIST);
    ASTIA_CHECK(manager.driver_count() == 1);

    device_handle first{};
    ASTIA_CHECK(manager.bind_device(typed_device(5), first) == 0);
    device_handle second{};
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

    driver_handle first_driver{};
    driver_handle second_driver{};
    ASTIA_CHECK(manager.register_driver(typed_descriptor(first_state, owner_id{1}),
                                        first_driver) == 0);
    ASTIA_CHECK(manager.register_driver(typed_descriptor(second_state, owner_id{2}),
                                        second_driver) == 0);

    device_handle first_device{};
    device_handle second_device{};
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

    driver_handle registered{};
    ASTIA_CHECK(manager.register_driver(typed_descriptor(state), registered) == 0);
    device_handle bound{};
    ASTIA_CHECK(manager.bind_device(typed_device(6), bound) == 0);
    ASTIA_CHECK(manager.unregister_driver(registered) == -EBUSY);
    ASTIA_CHECK(manager.unbind(bound) == 0);
    ASTIA_CHECK(manager.unregister_driver(registered) == 0);
}

ASTIA_TEST(driver_manager_owns_published_product_until_unbind) {
    using namespace astia::hw;

    driver_registry manager;
    typed_state     state;
    state.publish_product = true;

    driver_handle registered{};
    ASTIA_CHECK(manager.register_driver(typed_descriptor(state), registered) == 0);
    device_handle bound{};
    ASTIA_CHECK(manager.bind_device(typed_device(7), bound) == 0);
    ASTIA_CHECK(state.destroyed_products == 0);
    ASTIA_CHECK(manager.unbind(bound) == 0);
    ASTIA_CHECK(state.destroyed_products == 1);
}

ASTIA_TEST(driver_manager_shared_device_lifetime) {
    using namespace astia::hw;

    int             destroyed = 0;
    driver_registry manager;
    typed_state     state;

    astia::mm::shptr<test_info> info(new test_info(9, &destroyed));
    ASTIA_CHECK(info);
    astia::mm::shptr<test_info> external = info;

    driver_handle registered{};
    ASTIA_CHECK(manager.register_driver(typed_descriptor(state), registered) == 0);
    device_handle bound{};
    ASTIA_CHECK(manager.bind_device(
                    device_candidate(static_cast<astia::mm::shptr<test_info>&&>(info)),
                    bound) == 0);
    ASTIA_CHECK(!info);
    ASTIA_CHECK(external);
    ASTIA_CHECK(manager.unbind(bound) == 0);
    ASTIA_CHECK(destroyed == 0);
    external = {};
    ASTIA_CHECK(destroyed == 1);
}
