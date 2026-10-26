#include "../../proc.hpp"
#include "../../../sys/syscall.hpp"
#include "../../../sys/user_mem.hpp"
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
 i64 getgroups(i64 n,i64 out) {
  auto*p=current_process(); if(!p) return -ESRCH; if(n<0 || n>16) return -EINVAL;
  u32 values[16]{}; i64 rc=p->getgroups(static_cast<i32>(n),values);
  if(rc<0 || n==0) return rc;
  if(out==0 || !sys::copy_out(sys::user_range::bytes(static_cast<u64>(out), static_cast<usize>(rc)*sizeof(u32)), values)) return -EFAULT;
  return rc;
 }
 i64 setgroups(i64 n,i64 in) {
  auto*p=current_process(); if(!p) return -ESRCH; if(n<0 || n>16) return -EINVAL;
  u32 values[16]{};
  if(n!=0 && (in==0 || !sys::copy_in(values,sys::user_range::bytes(static_cast<u64>(in),static_cast<usize>(n)*sizeof(u32))))) return -EFAULT;
  return p->setgroups(static_cast<i32>(n),values);
 }
}
