#include <astia/api/aio.h>
#include <astia/api/errno.h>

#include "aio_registry_test_support.hpp"
#include "proc.hpp"
#include "process.hpp"
#include "support/aio_test_file.hpp"
#include "support/test.hpp"
#include "unix_control.hpp"

using namespace astia;

namespace {
    namespace aio = astia::io::aio;

    constexpr u32 cmsg_header_bytes = sizeof(astia_cmsghdr);
    constexpr u32 cmsg_align        = sizeof(u64);

    constexpr u32 aligned(u32 value) {
        return (value + cmsg_align - 1) & ~(cmsg_align - 1);
    }

    constexpr u32 rights_control_bytes = aligned(cmsg_header_bytes + sizeof(i32));

    void write_rights(u8* buffer, i32 fd) {
        auto* header = reinterpret_cast<astia_cmsghdr*>(buffer);

        header->cmsg_len   = cmsg_header_bytes + sizeof(i32);
        header->cmsg_level = SOL_SOCKET;
        header->cmsg_type  = ASTIA_SCM_RIGHTS;

        __builtin_memcpy(buffer + cmsg_header_bytes, &fd, sizeof fd);
    }

    mm::shptr<fs::open_file> fake_file() {
        return aio_test::make_open_file(new aio_test::fake_file);
    }
} // namespace

ASTIA_TEST(unix_control_parses_one_descriptor) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();

    const i32 source = install_descriptor(fake_file());

    u8 control[rights_control_bytes]{};

    write_rights(control, source);

    mm::shptr<fs::unix_control> parsed;

    ASTIA_CHECK(fs::parse_unix_control(*proc::current_process(),
                                       span<const u8>(control, sizeof control),
                                       parsed) == 0);
    ASTIA_CHECK(parsed);
    ASTIA_CHECK(parsed && parsed->count == 1);
    ASTIA_CHECK(parsed && parsed->files[0].get() ==
                              proc::current_process()->descriptor(fd_t{source}).get());
}

ASTIA_TEST(unix_control_rejects_malformed_headers) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();

    const i32 source = install_descriptor(fake_file());

    u8 control[rights_control_bytes]{};

    write_rights(control, source);

    auto* header = reinterpret_cast<astia_cmsghdr*>(control);

    header->cmsg_len = cmsg_header_bytes - 1;

    mm::shptr<fs::unix_control> parsed;

    ASTIA_CHECK(fs::parse_unix_control(*proc::current_process(),
                                       span<const u8>(control, sizeof control),
                                       parsed) == -EINVAL);

    write_rights(control, source);

    header->cmsg_len = sizeof control + 1;

    ASTIA_CHECK(fs::parse_unix_control(*proc::current_process(),
                                       span<const u8>(control, sizeof control),
                                       parsed) == -EINVAL);

    write_rights(control, source);

    header->cmsg_level = 0x7fff;

    ASTIA_CHECK(fs::parse_unix_control(*proc::current_process(),
                                       span<const u8>(control, sizeof control),
                                       parsed) == -EINVAL);

    write_rights(control, source);

    header->cmsg_type = ASTIA_SCM_CREDENTIALS;

    ASTIA_CHECK(fs::parse_unix_control(*proc::current_process(),
                                       span<const u8>(control, sizeof control),
                                       parsed) == -EINVAL);
}

ASTIA_TEST(unix_control_rejects_unknown_descriptor) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();

    u8 control[rights_control_bytes]{};

    write_rights(control, 99);

    mm::shptr<fs::unix_control> parsed;

    ASTIA_CHECK(fs::parse_unix_control(*proc::current_process(),
                                       span<const u8>(control, sizeof control),
                                       parsed) == -EBADF);
}

ASTIA_TEST(unix_control_build_installs_and_flags_cloexec) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();

    auto control = mm::share<fs::unix_control>();

    ASTIA_CHECK(control);

    control->count    = 1;
    control->files[0] = fake_file();

    u8 destination[fs::unix_control_max_bytes]{};

    u32 used      = 0;
    u32 msg_flags = 0;

    ASTIA_CHECK(fs::build_unix_control(
                    *proc::current_process(), *control, ASTIA_MSG_CMSG_CLOEXEC,
                    span<u8>(destination, sizeof destination), used, msg_flags) == 0);
    ASTIA_CHECK(msg_flags == 0);
    ASTIA_CHECK(used == rights_control_bytes);

    auto* header = reinterpret_cast<astia_cmsghdr*>(destination);

    ASTIA_CHECK(header->cmsg_level == SOL_SOCKET);
    ASTIA_CHECK(header->cmsg_type == ASTIA_SCM_RIGHTS);

    i32 received = -1;

    __builtin_memcpy(&received, destination + cmsg_header_bytes, sizeof received);

    ASTIA_CHECK(received >= 3);
    ASTIA_CHECK(descriptor_present(received));

    if (received >= 3)
        ASTIA_CHECK((proc::current_process()->descriptor_flags(fd_t{received}) &
                     ASTIA_FD_CLOEXEC) != 0);
}

ASTIA_TEST(unix_control_build_reports_truncation_without_installing) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();

    auto control = mm::share<fs::unix_control>();

    ASTIA_CHECK(control);

    control->count    = 1;
    control->files[0] = fake_file();

    u8 destination[cmsg_header_bytes]{};

    u32 used      = 0;
    u32 msg_flags = 0;

    ASTIA_CHECK(fs::build_unix_control(*proc::current_process(), *control, 0,
                                       span<u8>(destination, sizeof destination), used,
                                       msg_flags) == 0);
    ASTIA_CHECK(used == 0);
    ASTIA_CHECK((msg_flags & ASTIA_MSG_CTRUNC) != 0);

    for (i32 fd = 3; fd < static_cast<i32>(proc::max_process_fds); ++fd) {
        ASTIA_CHECK(!descriptor_present(fd));
    }
}

ASTIA_TEST(unix_control_build_rolls_back_on_descriptor_exhaustion) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();

    for (i32 fd = 3; fd < static_cast<i32>(proc::max_process_fds); ++fd) {
        ASTIA_CHECK(install_descriptor(fake_file()) >= 0);
    }

    const i32 free_fd = 4;

    clear_descriptor(free_fd);

    ASTIA_CHECK(!descriptor_present(free_fd));

    auto control = mm::share<fs::unix_control>();

    ASTIA_CHECK(control);

    control->count    = 2;
    control->files[0] = fake_file();
    control->files[1] = fake_file();

    u8 destination[fs::unix_control_max_bytes]{};

    u32 used      = 0;
    u32 msg_flags = 0;

    ASTIA_CHECK(fs::build_unix_control(*proc::current_process(), *control, 0,
                                       span<u8>(destination, sizeof destination), used,
                                       msg_flags) == -EMFILE);
    ASTIA_CHECK(used == 0);
    ASTIA_CHECK(!descriptor_present(free_fd));
}
