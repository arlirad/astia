#include <astia/log.hpp>
#include <astia/shutdown.hpp>

#include "support/test.hpp"

namespace astia::log::detail {
    void emit(level, const char*, astia::u32) {}

    void prefix(record_builder&, level, const char*, astia::u32) {}
} // namespace astia::log::detail

namespace {
    using astia::shutdown::handler;
    using astia::shutdown::detail::dispatch_executor;

    constexpr astia::usz fake_capacity = 8;

    struct fake_task {
        handler fn      = nullptr;
        void*   context = nullptr;
        int     result  = -1; // -1 running, 0 failed, 1 succeeded
        bool    blocked = false;
    };

    struct marker {
        int  id;
        bool result;
    };

    int calls[16]{};
    int call_count = 0;

    fake_task  tasks[fake_capacity]{};
    astia::usz task_count      = 0;
    astia::usz starts          = 0;
    astia::usz abandons        = 0;
    astia::usz relaxes         = 0;
    astia::u64 clock           = 0;
    astia::u64 clock_step      = astia::shutdown::handler_timeout_ns;
    bool       block_next      = false;
    bool       clock_available = true;

    bool mark(void* context) {
        auto* value = static_cast<marker*>(context);

        calls[call_count++] = value->id;

        return value->result;
    }

    bool fake_start(handler fn, void* context, void** task) {
        if (fn == nullptr || task == nullptr || task_count == fake_capacity)
            return false;

        fake_task& value = tasks[task_count];

        value.fn      = fn;
        value.context = context;
        value.result  = -1;
        value.blocked = block_next;
        block_next    = false;

        *task = &value;

        ++task_count;
        ++starts;

        return true;
    }

    int fake_poll(void* task) {
        auto* value = static_cast<fake_task*>(task);

        if (value->result == -1 && !value->blocked)
            value->result = value->fn(value->context) ? 1 : 0;

        if (value->result == -1)
            return 0;

        return value->result == 1 ? 1 : 2;
    }

    void fake_abandon(void*) {
        ++abandons;
    }

    void fake_relax() {
        ++relaxes;

        clock += clock_step;
    }

    astia::u64 fake_now_ns() {
        return clock_available ? clock : 0;
    }

    const dispatch_executor fake_backend{
        fake_start, fake_poll, fake_abandon, fake_relax, fake_now_ns,
    };

    void reset_fake() {
        call_count      = 0;
        task_count      = 0;
        starts          = 0;
        abandons        = 0;
        relaxes         = 0;
        clock           = 0;
        clock_step      = astia::shutdown::handler_timeout_ns;
        block_next      = false;
        clock_available = true;

        for (int& call : calls) {
            call = 0;
        }

        for (fake_task& task : tasks) {
            task = fake_task{};
        }
    }

    bool register_marker(astia::shutdown::tier level, const char* name, marker& value) {
        return astia::shutdown::register_handler(level, name, mark, &value);
    }
} // namespace

ASTIA_TEST(shutdown_watchdog_abandons_blocking_handler_and_runs_final) {
    astia::shutdown::detail::reset_for_test();
    reset_fake();

    ASTIA_CHECK(astia::shutdown::detail::set_dispatch_executor(&fake_backend));

    block_next = true;

    marker blocking{1, true};
    marker flush{2, true};
    marker platform{3, true};
    marker final{4, true};

    ASTIA_CHECK(register_marker(astia::shutdown::tier::prepare, "blocking", blocking));
    ASTIA_CHECK(register_marker(astia::shutdown::tier::flush, "flush", flush));
    ASTIA_CHECK(register_marker(astia::shutdown::tier::platform, "platform", platform));
    ASTIA_CHECK(register_marker(astia::shutdown::tier::final, "final", final));

    ASTIA_CHECK(!astia::shutdown::initiate());
    ASTIA_CHECK(abandons == 1);
    ASTIA_CHECK(starts == 3);
    ASTIA_CHECK(call_count == 3);
    ASTIA_CHECK(calls[0] == 2);
    ASTIA_CHECK(calls[1] == 3);
    ASTIA_CHECK(calls[2] == 4);
    ASTIA_CHECK(!astia::shutdown::initiate());
}

ASTIA_TEST(shutdown_executor_reports_handler_failure) {
    astia::shutdown::detail::reset_for_test();
    reset_fake();

    ASTIA_CHECK(astia::shutdown::detail::set_dispatch_executor(&fake_backend));

    marker failing{1, false};
    marker platform{2, true};
    marker final{3, true};

    ASTIA_CHECK(register_marker(astia::shutdown::tier::prepare, "failing", failing));
    ASTIA_CHECK(register_marker(astia::shutdown::tier::platform, "platform", platform));
    ASTIA_CHECK(register_marker(astia::shutdown::tier::final, "final", final));

    ASTIA_CHECK(!astia::shutdown::initiate());
    ASTIA_CHECK(abandons == 0);
    ASTIA_CHECK(starts == 2);
    ASTIA_CHECK(call_count == 3);
    ASTIA_CHECK(calls[0] == 1);
    ASTIA_CHECK(calls[1] == 2);
    ASTIA_CHECK(calls[2] == 3);
}

ASTIA_TEST(shutdown_watchdog_backstops_a_missing_clock) {
    astia::shutdown::detail::reset_for_test();
    reset_fake();

    ASTIA_CHECK(astia::shutdown::detail::set_dispatch_executor(&fake_backend));

    block_next      = true;
    clock_available = false;

    marker blocking{1, true};
    marker platform{2, true};
    marker final{3, true};

    ASTIA_CHECK(register_marker(astia::shutdown::tier::prepare, "blocking", blocking));
    ASTIA_CHECK(register_marker(astia::shutdown::tier::platform, "platform", platform));
    ASTIA_CHECK(register_marker(astia::shutdown::tier::final, "final", final));

    ASTIA_CHECK(!astia::shutdown::initiate());
    ASTIA_CHECK(abandons == 1);
    ASTIA_CHECK(relaxes == astia::shutdown::handler_max_polls);
    ASTIA_CHECK(call_count == 2);
    ASTIA_CHECK(calls[0] == 2);
    ASTIA_CHECK(calls[1] == 3);
}

ASTIA_TEST(shutdown_final_override_is_not_dispatched_through_executor) {
    astia::shutdown::detail::reset_for_test();
    reset_fake();

    ASTIA_CHECK(astia::shutdown::detail::set_dispatch_executor(&fake_backend));

    marker platform{1, true};
    marker override_final{2, true};

    ASTIA_CHECK(register_marker(astia::shutdown::tier::platform, "platform", platform));

    ASTIA_CHECK(astia::shutdown::initiate(mark, &override_final));
    ASTIA_CHECK(starts == 1);
    ASTIA_CHECK(abandons == 0);
    ASTIA_CHECK(call_count == 2);
    ASTIA_CHECK(calls[0] == 1);
    ASTIA_CHECK(calls[1] == 2);
}

ASTIA_TEST(shutdown_executor_rejects_incomplete_table) {
    astia::shutdown::detail::reset_for_test();

    dispatch_executor incomplete{};

    ASTIA_CHECK(!astia::shutdown::detail::set_dispatch_executor(&incomplete));
    ASTIA_CHECK(astia::shutdown::detail::set_dispatch_executor(nullptr));
}
