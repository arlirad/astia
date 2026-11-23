#pragma once

#include "ring.hpp"

namespace astia::io::aio::dispatch {

    mm::shptr<fs::open_file> file_for(proc::process* current, i32 fd);
    mm::shptr<fs::open_file> file_for(i32 fd);
    long resolve_at(proc::process* current, i32 raw_dirfd, const char* path, char* out,
                    usize capacity);
    bool parent_writable(proc::process* current, const char* path);
    bool submit_socket_request(ring::ring_slot& slot, astia_rings* ring,
                               const astia_sqe&                sqe,
                               const mm::shptr<fs::open_file>& object,
                               const span<u8>* entries, u32 count,
                               ::astia::io::direction direction, bool control = false,
                               bool socket_accept = false, core::cltoken token = {});
    bool submit_io(ring::ring_slot& slot, astia_rings* ring, const astia_sqe& sqe,
                   ::astia::io::direction direction, bool vectored,
                   core::cltoken token = {});
    bool publish_pending(astia_rings* ring, ring::parked_op& op);

    bool dispatch_one(ring::ring_slot& slot, astia_rings* ring, const astia_sqe& sqe);
    void retry_pending(ring::ring_slot& slot, astia_rings* ring);
    void resume_parked(proc::process* owner, astia_rings* ring, ring::parked_op& op);
    void complete_control(proc::process* owner, astia_rings* ring, ring::parked_op& op,
                          i32 result);
    u64  poll_now_ns();

} // namespace astia::io::aio::dispatch
