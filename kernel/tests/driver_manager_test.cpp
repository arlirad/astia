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
#include <astia/dev/device.hpp>
#include <astia/dev/driver.hpp>

namespace {

    class test_info final : public astia::hw::device_info {
        public:
        explicit test_info(astia::u32 value) : value_(value) {}

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
    };

    astia::mm::shptr<astia::hw::device_info> device_candidate(
        astia::mm::shptr<test_info>&& value) {
        return astia::mm::shptr<astia::hw::device_info>(
            static_cast<astia::mm::shptr<test_info>&&>(value));
    }

    class test_device final : public astia::dev::device {
        public:
        explicit test_device(int* destroyed)
            : device("driver-test", astia::dev::dv_sync), destroyed_(destroyed) {}

        ~test_device() {
            ++*destroyed_;
        }

        long read(void*, astia::u32, astia::u64) override {
            return 0;
        }

        long write(const void*, astia::u32, astia::u64) override {
            return 0;
        }

        private:
        int* destroyed_;
    };

    class test_driver final : public astia::hw::driver {
        public:
        test_driver(bool should_match, bool should_probe, int* destroyed)
            : should_match_(should_match), should_probe_(should_probe), destroyed_(destroyed) {}

        const char* name() const override {
            return "driver-test";
        }

        bool match(const astia::hw::device_info& info) const override {
            const auto& value = static_cast<const test_info&>(info);
            matched_value_ = value.value();
            ++match_count_;
            return should_match_;
        }

        astia::mm::shptr<astia::dev::device> probe(
            const astia::hw::device_info& info) override {
            const auto& value = static_cast<const test_info&>(info);
            probed_value_ = value.value();
            ++probe_count_;
            return should_probe_
                       ? astia::mm::shptr<astia::dev::device>(
                             new test_device(destroyed_))
                       : astia::mm::shptr<astia::dev::device>{};
        }

        void remove(const astia::hw::device_info& info,
                    const astia::mm::shptr<astia::dev::device>& value) override {
            const auto& candidate = static_cast<const test_info&>(info);
            removed_value_ = candidate.value();
            removed_device_ = value.get();
            ++remove_count_;
        }

        bool should_match_;
        bool should_probe_;
        int* destroyed_;
        mutable int match_count_ = 0;
        int probe_count_ = 0;
        int remove_count_ = 0;
        mutable astia::u32 matched_value_ = 0;
        astia::u32 probed_value_ = 0;
        astia::u32 removed_value_ = 0;
        astia::dev::device* removed_device_ = nullptr;
    };

}

ASTIA_TEST(driver_manager_passes_candidate_to_match_and_probe) {
    int destroyed = 0;
    test_driver driver(true, true, &destroyed);
    astia::hw::driver_registry manager;
    astia::mm::shptr<test_info> info(new test_info(42));
    ASTIA_CHECK(info);

    ASTIA_CHECK(manager.register_driver(driver));
    ASTIA_CHECK(manager.bind(device_candidate(static_cast<astia::mm::shptr<test_info>&&>(info))));
    ASTIA_CHECK(driver.match_count_ == 1);
    ASTIA_CHECK(driver.probe_count_ == 1);
    ASTIA_CHECK(driver.matched_value_ == 42);
    ASTIA_CHECK(driver.probed_value_ == 42);
    ASTIA_CHECK(manager.device_count() == 1);
    ASTIA_CHECK(manager.device_at(0));
    manager.unbind_all();
    ASTIA_CHECK(driver.remove_count_ == 1);
    ASTIA_CHECK(driver.removed_value_ == 42);
}

ASTIA_TEST(driver_manager_rejects_failed_probe) {
    int destroyed = 0;
    test_driver driver(true, false, &destroyed);
    astia::hw::driver_registry manager;
    astia::mm::shptr<test_info> info(new test_info(7));
    ASTIA_CHECK(info);

    ASTIA_CHECK(manager.register_driver(driver));
    ASTIA_CHECK(!manager.bind(device_candidate(static_cast<astia::mm::shptr<test_info>&&>(info))));
    ASTIA_CHECK(driver.match_count_ == 1);
    ASTIA_CHECK(driver.probe_count_ == 1);
    ASTIA_CHECK(manager.device_count() == 0);
    ASTIA_CHECK(destroyed == 0);
}

ASTIA_TEST(driver_manager_shared_device_lifetime) {
    int destroyed = 0;
    test_driver driver(true, true, &destroyed);
    astia::hw::driver_registry manager;
    astia::mm::shptr<test_info> info(new test_info(9));
    ASTIA_CHECK(info);

    ASTIA_CHECK(manager.register_driver(driver));
    auto candidate = info;
    ASTIA_CHECK(manager.bind(device_candidate(static_cast<astia::mm::shptr<test_info>&&>(info))));
    ASTIA_CHECK(!info);
    ASTIA_CHECK(candidate);
    auto retained = manager.device_at(0);
    astia::dev::device* address = retained.get();
    manager.unbind_all();
    ASTIA_CHECK(driver.remove_count_ == 1);
    ASTIA_CHECK(driver.removed_device_ == address);
    ASTIA_CHECK(destroyed == 0);
    retained = {};
    ASTIA_CHECK(destroyed == 1);
}

ASTIA_TEST(driver_manager_rejects_equal_candidates) {
    int destroyed = 0;
    test_driver driver(true, true, &destroyed);
    astia::hw::driver_registry manager;
    astia::mm::shptr<test_info> first(new test_info(11));
    astia::mm::shptr<test_info> second(new test_info(11));

    ASTIA_CHECK(manager.register_driver(driver));
    ASTIA_CHECK(manager.bind(device_candidate(static_cast<astia::mm::shptr<test_info>&&>(first))));
    ASTIA_CHECK(!manager.bind(device_candidate(static_cast<astia::mm::shptr<test_info>&&>(second))));
    ASTIA_CHECK(manager.device_count() == 1);
    manager.unbind_all();
}
