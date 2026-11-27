#include <astia/core/completion.hpp>

#include "support/test.hpp"

ASTIA_TEST(completion_stores_value_once) {
    astia::core::completion<int> completion;

    ASTIA_CHECK(!completion.done());
    ASTIA_CHECK(completion.complete(77));
    ASTIA_CHECK(completion.done());
    ASTIA_CHECK(!completion.complete(88));

    const auto result = completion.await_resume();

    ASTIA_CHECK(result);
    ASTIA_CHECK(result.value == 77);
}

ASTIA_TEST(completion_stores_error_once) {
    astia::core::completion<int> completion;

    ASTIA_CHECK(completion.fail(astia::error_t::eio));
    ASTIA_CHECK(completion.done());
    ASTIA_CHECK(!completion.cancel());

    const auto result = completion.await_resume();

    ASTIA_CHECK(!result);
    ASTIA_CHECK(result.error == astia::error_t::eio);
}

ASTIA_TEST(completion_cancellation_is_one_shot) {
    astia::core::completion<int> completion;

    ASTIA_CHECK(completion.cancel());
    ASTIA_CHECK(completion.done());
    ASTIA_CHECK(!completion.complete(1));

    const auto result = completion.await_resume();

    ASTIA_CHECK(!result);
    ASTIA_CHECK(result.error == astia::error_t::eagain);
}
