#include "test.hpp"

namespace astia::core {
    void initialize_resume_queues() __attribute__((weak));
}

int main() {
    if (astia::core::initialize_resume_queues != nullptr)
        astia::core::initialize_resume_queues();
    return astia_test::run_all();
}
