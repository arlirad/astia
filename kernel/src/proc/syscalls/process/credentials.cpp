#include "../../../sys/syscall.hpp"
#include "../../../sys/user_mem.hpp"
#include "../../proc.hpp"
#include <astia/api/errno.h>
namespace astia::proc::syscall {
    i64 getuid() {
        auto* p = current_process();
        return p ? p->uid() : -ESRCH;
    }
    i64 geteuid() {
        auto* p = current_process();
        return p ? p->euid() : -ESRCH;
    }
    i64 getgid() {
        auto* p = current_process();
        return p ? p->gid() : -ESRCH;
    }
    i64 getegid() {
        auto* p = current_process();
        return p ? p->egid() : -ESRCH;
    }
    i64 setuid(i64 v) {
        auto* p = current_process();
        if (!p)
            return -ESRCH;
        if (v < 0 || static_cast<u64>(v) > ~u32{0})
            return -EINVAL;
        return p->setuid(static_cast<u32>(v));
    }
    i64 seteuid(i64 v) {
        auto* p = current_process();
        if (!p)
            return -ESRCH;
        if (v < 0 || static_cast<u64>(v) > ~u32{0})
            return -EINVAL;
        return p->seteuid(static_cast<u32>(v));
    }
    i64 setgid(i64 v) {
        auto* p = current_process();
        if (!p)
            return -ESRCH;
        if (v < 0 || static_cast<u64>(v) > ~u32{0})
            return -EINVAL;
        return p->setgid(static_cast<u32>(v));
    }
    i64 setegid(i64 v) {
        auto* p = current_process();
        if (!p)
            return -ESRCH;
        if (v < 0 || static_cast<u64>(v) > ~u32{0})
            return -EINVAL;
        return p->setegid(static_cast<u32>(v));
    }
    i64 setreuid(i64 r, i64 e) {
        auto* p = current_process();
        if (!p)
            return -ESRCH;
        if ((r < -1 || static_cast<u64>(r) > ~u32{0}) ||
            (e < -1 || static_cast<u64>(e) > ~u32{0}))
            return -EINVAL;
        return p->setreuid(static_cast<u32>(r), static_cast<u32>(e));
    }
    i64 setregid(i64 r, i64 e) {
        auto* p = current_process();
        if (!p)
            return -ESRCH;
        if ((r < -1 || static_cast<u64>(r) > ~u32{0}) ||
            (e < -1 || static_cast<u64>(e) > ~u32{0}))
            return -EINVAL;
        return p->setregid(static_cast<u32>(r), static_cast<u32>(e));
    }
    i64 getgroups(i64 n, i64 out) {
        auto* p = current_process();
        if (!p)
            return -ESRCH;
        if (n < 0 || n > static_cast<i64>(max_supplementary_groups))
            return -EINVAL;
        u32 values[max_supplementary_groups]{};
        i64 rc = p->getgroups(static_cast<i32>(n), values);
        if (rc < 0 || n == 0)
            return rc;
        if (out == 0 ||
            !sys::copy_out(sys::user_range::bytes(static_cast<u64>(out),
                                                  static_cast<usize>(rc) * sizeof(u32)),
                           values))
            return -EFAULT;
        return rc;
    }
    i64 setgroups(i64 n, i64 in) {
        auto* p = current_process();
        if (!p)
            return -ESRCH;
        if (n < 0 || n > static_cast<i64>(max_supplementary_groups))
            return -EINVAL;
        u32 values[max_supplementary_groups]{};
        if (n != 0 &&
            (in == 0 || !sys::copy_in(values, sys::user_range::bytes(
                                                  static_cast<u64>(in),
                                                  static_cast<usize>(n) * sizeof(u32)))))
            return -EFAULT;
        return p->setgroups(static_cast<i32>(n), values);
    }
}
