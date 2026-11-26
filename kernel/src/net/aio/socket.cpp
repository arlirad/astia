#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {
    namespace {
        enum class socket_domain : u32 {
            unix_domain = AF_UNIX,
        };

        enum class socket_type : u32 {
            stream   = SOCK_STREAM,
            datagram = SOCK_DGRAM,
        };

        struct socket_request {
            socket_domain domain;
            socket_type   type;
            u32           aux;

            static constexpr socket_request from_sqe(const astia_sqe& s) {
                return {static_cast<socket_domain>(static_cast<u32>(s.fd)),
                        static_cast<socket_type>(s.op_flags), s.aux};
            }

            constexpr bool supported() const {
                return domain == socket_domain::unix_domain &&
                       (type == socket_type::stream || type == socket_type::datagram) &&
                       aux == 0;
            }
        };

        static_assert(static_cast<u32>(socket_domain::unix_domain) == AF_UNIX);
        static_assert(static_cast<u32>(socket_type::stream) == SOCK_STREAM);
        static_assert(static_cast<u32>(socket_type::datagram) == SOCK_DGRAM);
    } // namespace

    struct socket : handler<socket, opcode::socket> {
        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               astia_sqe sqe, [[maybe_unused]] core::cltoken token) noexcept {
            const socket_request request = socket_request::from_sqe(sqe);

            if (!request.supported())
                co_return -EAFNOSUPPORT;

            proc::process*           current = proc::current_process();
            mm::shptr<fs::open_file> handle;

            long rc = fs::create_unix_socket(handle, static_cast<u32>(request.type));

            if (rc < 0 || !current)
                co_return (i32)(rc < 0 ? rc : -ESRCH);

            const auto installed = current->allocate_descriptor(handle);

            co_return installed ? installed.value.value : -EMFILE;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor socket_descriptor = socket::descriptor;
} // namespace astia::io::aio::operations
