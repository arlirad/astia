#include "../../proc.hpp"
#include "../../../sys/syscall.hpp"
#include <astia/api/errno.h>
namespace astia::proc::syscall {
 i64 getuid() { auto*p=current_process(); return p?p->uid():-ESRCH; }
 i64 geteuid() { auto*p=current_process(); return p?p->euid():-ESRCH; }
 i64 getgid() { auto*p=current_process(); return p?p->gid():-ESRCH; }
 i64 getegid() { auto*p=current_process(); return p?p->egid():-ESRCH; }
 i64 setuid(i64 v) { auto*p=current_process(); return p?p->setuid(v):-ESRCH; }
 i64 seteuid(i64 v) { auto*p=current_process(); return p?p->seteuid(v):-ESRCH; }
 i64 setgid(i64 v) { auto*p=current_process(); return p?p->setgid(v):-ESRCH; }
 i64 setegid(i64 v) { auto*p=current_process(); return p?p->setegid(v):-ESRCH; }
 i64 setreuid(i64 r,i64 e) { auto*p=current_process(); return p?p->setreuid(r,e):-ESRCH; }
 i64 setregid(i64 r,i64 e) { auto*p=current_process(); return p?p->setregid(r,e):-ESRCH; }
}
