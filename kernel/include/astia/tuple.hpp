#pragma once

namespace astia {
    template <typename First, typename Second>
    struct tuple {
        First  first{};
        Second second{};

        tuple() = default;

        tuple(First first_value, Second second_value)
            : first(static_cast<First&&>(first_value)),
              second(static_cast<Second&&>(second_value)) {}
    };
} // namespace astia
