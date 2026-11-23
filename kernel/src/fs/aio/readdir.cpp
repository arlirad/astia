#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct readdir : handler<readdir, opcode::readdir> {
        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               astia_sqe sqe, [[maybe_unused]] core::cltoken token) noexcept {
            char path[256];
            if (!memory::copy_in_string(span<char>(path, sizeof path),
                                        memory::user_ptr<char>(sqe.offset)))
                co_return -EFAULT;
            auto* current = proc::current_process();
            char  resolved[256];
            if (current == nullptr)
                co_return -ESRCH;
            long resolve_rc = current->compose_path(path, resolved, sizeof resolved);
            if (resolve_rc >= 0)
                resolve_rc = fs::resolve_path(resolved, resolved, sizeof resolved, true);
            if (resolve_rc < 0)
                co_return (i32) resolve_rc;
            const auto out = memory::user_ptr<astia_dirent>(sqe.address).validate();
            if (!out)
                co_return -EFAULT;
            astia_dirent  result{};
            fs::file_type type = fs::directory_unknown;
            long rc = fs::read_directory(resolved, sqe.fd < 0 ? 0 : sqe.fd, result.name,
                                         sizeof result.name, &type);
            result.type = static_cast<u32>(type);
            if (rc > 0) {
                u32 length = 0;
                while (length < sizeof result.name && result.name[length] != '\0')
                    ++length;
                result.namelen = length;
            }
            if (!memory::copy_out(out.value.bytes(), &result))
                co_return -EFAULT;
            co_return static_cast<i32>(rc);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor readdir_descriptor = readdir::descriptor;

} // namespace astia::io::aio::operations
