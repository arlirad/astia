#pragma once

#include "aio_ring.hpp"

namespace astia::sys::aio_dispatch {

    mm::shptr<fs::open_file> file_for(proc::process* current, i32 fd);
    mm::shptr<fs::open_file> file_for(i32 fd);
    long resolve_at(proc::process* current, i32 raw_dirfd, const char* path,
                    char* out, usize capacity);
    bool parent_writable(proc::process* current, const char* path);
    bool submit_socket_request(
        aio_ring::ring_slot& slot, astia_rings* ring, const astia_sqe& sqe,
        const mm::shptr<fs::open_file>& object, const span<u8>* entries, u32 count,
        io::direction direction, bool control = false, bool socket_accept = false);

    bool dispatch_one(aio_ring::ring_slot& slot, astia_rings* ring,
                      const astia_sqe& sqe);
    void retry_pending(aio_ring::ring_slot& slot, astia_rings* ring);
    void resume_parked(astia_rings* ring, aio_ring::parked_op& op);
    void complete_control(proc::process* owner, astia_rings* ring,
                          aio_ring::parked_op& op, i32 result);
    u64 poll_now_ns();

} // namespace astia::sys::aio_dispatch
