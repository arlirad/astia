#pragma once
#include <astia/fs.hpp>
#include <astia/api/aio.h>

namespace astia::fs {
long create_unix_socket(mm::shptr<open_file>& out);
long unix_bind(open_file&, const astia_sockaddr&);
long unix_listen(open_file&, u32 backlog);
long unix_connect(open_file&, const astia_sockaddr&);
long unix_accept(open_file&, mm::shptr<open_file>& out);
long unix_socketpair(mm::shptr<open_file>& left, mm::shptr<open_file>& right,
                     u32 type);
long unix_shutdown(open_file&, u32 how);
}
