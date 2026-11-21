#include <astia/boot/init.hpp>

#include "support/test.hpp"

namespace {
    bool string_equal(const char* first, const char* second) {
        while (*first != '\0' && *first == *second) {
            ++first;
            ++second;
        }
        return *first == *second;
    }
}

ASTIA_TEST(boot_init_selection) {
    using astia::boot::init_option_error;
    using astia::boot::select_init;

    auto selection = select_init(nullptr);
    ASTIA_CHECK(selection.error == init_option_error::none);
    ASTIA_CHECK(string_equal(selection.path, "/sbin/init"));

    selection = select_init("quiet --init /bin/zsh console=ttyS0");
    ASTIA_CHECK(selection.error == init_option_error::none);
    ASTIA_CHECK(string_equal(selection.path, "/bin/zsh"));

    ASTIA_CHECK(select_init("--init").error == init_option_error::missing_path);
    ASTIA_CHECK(select_init("--init relative").error == init_option_error::invalid_path);
    ASTIA_CHECK(select_init("--init /bin/../bin/zsh").error ==
                init_option_error::invalid_path);
    ASTIA_CHECK(select_init("--init /bin//zsh").error == init_option_error::invalid_path);
    ASTIA_CHECK(select_init("--init=/bin/zsh").error == init_option_error::invalid_path);
    ASTIA_CHECK(select_init("--init /bin/zsh --init /sbin/init").error ==
                init_option_error::duplicate);

    char       long_command[300]{};
    const char prefix[] = "--init /";
    for (unsigned long index = 0; index < sizeof(prefix) - 1; ++index)
        long_command[index] = prefix[index];
    for (unsigned long index = sizeof(prefix) - 1; index < sizeof(long_command) - 1;
         ++index)
        long_command[index] = 'a';
    ASTIA_CHECK(select_init(long_command).error == init_option_error::path_too_long);
}
