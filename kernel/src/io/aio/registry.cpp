#include "registry.hpp"

#include <astia/api/errno.h>

#include "core_handlers.hpp"
#include "filesystem.hpp"
#include "readiness.hpp"
#include "ring.hpp"
#include "socket.hpp"

namespace astia::io::aio::registry {

    namespace {

        constexpr descriptor manifest[] = {
            core::nop::describe(),
            core::read::describe(),
            core::write::describe(),
            core::readv::describe(),
            core::writev::describe(),
            filesystem::operation<opcode::openat>::describe(),
            filesystem::operation<opcode::close>::describe(),
            filesystem::operation<opcode::statx>::describe(),
            filesystem::operation<opcode::fsync>::describe(),
            filesystem::operation<opcode::unlinkat>::describe(),
            filesystem::operation<opcode::mkdirat>::describe(),
            filesystem::operation<opcode::renameat>::describe(),
            readiness::operation<opcode::sleep>::describe(),
            core::wait::describe(),
            readiness::operation<opcode::poll_add>::describe(),
            core::spawn::describe(),
            core::cancel::describe(),
            socket::operation<opcode::accept>::describe(),
            socket::operation<opcode::connect>::describe(),
            core::send::describe(),
            core::recv::describe(),
            core::dup::describe(),
            core::dup2::describe(),
            filesystem::operation<opcode::readdir>::describe(),
            core::ioctl::describe(),
            socket::operation<opcode::pipe>::describe(),
            filesystem::operation<opcode::chmod>::describe(),
            filesystem::operation<opcode::utimensat>::describe(),
            filesystem::operation<opcode::symlinkat>::describe(),
            filesystem::operation<opcode::readlinkat>::describe(),
            filesystem::operation<opcode::chown>::describe(),
            socket::operation<opcode::socket>::describe(),
            socket::operation<opcode::bind>::describe(),
            socket::operation<opcode::listen>::describe(),
            socket::operation<opcode::socketpair>::describe(),
            socket::operation<opcode::shutdown>::describe(),
            core::fcntl::describe(),
            socket::operation<opcode::sendto>::describe(),
            socket::operation<opcode::recvfrom>::describe(),
            core::eventfd_create::describe(),
            filesystem::operation<opcode::linkat>::describe(),
            filesystem::operation<opcode::ftruncate>::describe(),
            socket::operation<opcode::getsockopt>::describe(),
        };

        static_assert(valid_manifest(manifest));

    } // namespace

    const descriptor* entries() {
        return manifest;
    }

    usize size() {
        return sizeof(manifest) / sizeof(manifest[0]);
    }

    bool dispatch(ring::ring_slot& slot, astia_rings* ring, const astia_sqe& sqe) {
        if (sqe.op >= opcode_count)
            return ring::cq_post_guarded(ring, sqe.user_data, -ENOSYS);
        return manifest[sqe.op].invoke(slot, ring, sqe);
    }

} // namespace astia::io::aio::registry
