#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {
    struct readdir : handler<readdir, opcode::readdir> {
        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               astia_sqe sqe, [[maybe_unused]] core::cltoken token) noexcept {
            const bool by_handle = (sqe.op_flags & ASTIA_AIO_READDIR_FD) != 0;

            char path[256];
            char resolved[256];

            fs::file_type type = fs::directory_unknown;
            astia_dirent  result{};

            mm::shptr<fs::open_file> handle;

            u64  index;
            long rc;

            if (by_handle) {
                handle = file_for(sqe.fd);
                if (!handle)
                    co_return -EBADF;

                index = sqe.offset;
            }
            else {
                if (!memory::copy_in_string(span<char>(path, sizeof path),
                                            memory::user_ptr<char>(sqe.offset)))
                    co_return -EFAULT;

                auto* current = proc::current_process();
                if (current == nullptr)
                    co_return -ESRCH;

                const long resolve_rc =
                    current->resolve_path(path, resolved, sizeof resolved, true);
                if (resolve_rc < 0)
                    co_return (i32) resolve_rc;

                index = sqe.fd < 0 ? 0 : static_cast<u64>(sqe.fd);
            }

            const auto out = memory::user_ptr<astia_dirent>(sqe.address).validate();
            if (!out)
                co_return -EFAULT;

            rc = by_handle ? fs::read_directory(handle, index, result.name,
                                                sizeof result.name, &type)
                           : fs::read_directory(resolved, index, result.name,
                                                sizeof result.name, &type);

            result.type = static_cast<u32>(type);

            if (rc > 0) {
                u32 length = 0;

                while (length < sizeof result.name && result.name[length] != '\0') {
                    ++length;
                }

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
