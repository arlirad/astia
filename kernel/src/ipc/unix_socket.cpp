#include "unix_socket.hpp"
#include "../sys/aio.hpp"
#include "../proc/process.hpp"
#include "proc.hpp"
#include <astia/api/errno.h>
#include <astia/core/spinlock.hpp>
#include <astia/mm/share.hpp>

namespace astia::fs {
namespace {
constexpr u32 cap = 4096, max_names = 32, max_backlog = 16;
struct channel {
    u8 data[2][cap]{}; u32 head[2]{}, tail[2]{}; bool closed[2]{};
};
class unix_file;
struct name_entry { bool used{}; char name[path_max]{}; unix_file* listener{}; };
name_entry names[max_names];
core::spinlock names_lock;

    class unix_file final : public file {
public:
    enum class state : u8 { fresh, listener, connected };
    unix_file() = default;
    long read(void* b, u32 n, u64) override {
        if (state_ != state::connected) return -ENOTCONN;
        if (!ch_) return -ENOTCONN;
        const u32 side = side_ ^ 1u, h = __atomic_load_n(&ch_->head[side], __ATOMIC_ACQUIRE),
                  t = __atomic_load_n(&ch_->tail[side], __ATOMIC_RELAXED);
        if (h == t) return ch_->closed[side_] ? 0 : 0;
        u32 count = h - t < n ? h - t : n; u8* out = static_cast<u8*>(b);
        for (u32 i=0;i<count;++i) out[i] = ch_->data[side][(t+i)&(cap-1)];
        __atomic_store_n(&ch_->tail[side], t+count, __ATOMIC_RELEASE);
        sys::io_device_ready_context(this); return count;
    }
    long write(const void* b, u32 n, u64) override {
        if (state_ != state::connected) return -ENOTCONN;
        if (!ch_) return -ENOTCONN;
        const u32 side = side_, peer = side_^1u;
        if (ch_->closed[peer]) return -EPIPE;
        u32 h=__atomic_load_n(&ch_->head[side],__ATOMIC_RELAXED), t=__atomic_load_n(&ch_->tail[side],__ATOMIC_ACQUIRE);
        u32 free=cap-(h-t); if (!free) return 0; u32 count=free<n?free:n; const u8* in=static_cast<const u8*>(b);
        for(u32 i=0;i<count;++i) ch_->data[side][(h+i)&(cap-1)]=in[i];
        __atomic_store_n(&ch_->head[side],h+count,__ATOMIC_RELEASE); sys::io_device_ready_context(peer_file_); return count;
    }
        long submit(io_request& r) override {
            if (state_ == state::listener) {
                auto accepted = dequeue();
                if (!accepted) {
                    r.stage(1);
                    return 0;
                }
                auto* current = proc::current_process();
                if (!current)
                    return -ESRCH;
                for (i32 fd = 3; fd < static_cast<i32>(proc::max_process_fds); ++fd) {
                    if (!current->descriptor(fd_t{fd}) &&
                        current->set_descriptor(fd_t{fd}, accepted)) {
                        r.complete(true);
                        return fd;
                    }
                }
                return -EMFILE;
            }
            long n=r.direction()==io_direction::input?read(r.buffer(),r.length(),0):write(r.buffer(),r.length(),0);
        if(n==0 && r.direction()==io_direction::input && ch_ && ch_->closed[side_]) r.complete(true);
        else if(n==0) r.stage(1);
        return n;
    }
    void close(u32) override {
        if (bound_) { core::spinlock_guard g(names_lock); for (auto& e:names) if (e.listener==this) e= name_entry{}; bound_=false; }
        if(ch_){ ch_->closed[side_]=true; if(peer_file_) sys::io_device_ready_context(peer_file_); }
    }
    void attach(const mm::shptr<channel>& c, u32 s, unix_file* peer) { ch_=c; side_=s; peer_file_=peer; state_=state::connected; }
    state kind() const { return state_; }
    void make_listener(u32 b) { state_=state::listener; backlog_=b; }
    void mark_bound() { bound_=true; }
    bool enqueue(const mm::shptr<open_file>& f) { if(queue_count_>=backlog_) return false; queue_[queue_tail_]=f; queue_tail_=(queue_tail_+1)%max_backlog; ++queue_count_; sys::io_device_ready_context(this); return true; }
    mm::shptr<open_file> dequeue() { if(!queue_count_) return {}; auto x=queue_[queue_head_]; queue_[queue_head_]={}; queue_head_=(queue_head_+1)%max_backlog; --queue_count_; return x; }
private:
    mm::shptr<channel> ch_; u32 side_{}; unix_file* peer_file_{}; state state_{state::fresh}; u32 backlog_{}; bool bound_{};
    mm::shptr<open_file> queue_[max_backlog]{}; u32 queue_head_{},queue_tail_{},queue_count_{};
};
bool path(const astia_sockaddr& a,char* out){ if(a.family!=AF_UNIX||a.len<3||a.len>sizeof(a.data)) return false; u32 n=a.len-2; if(n>=path_max) return false; for(u32 i=0;i<n;++i) out[i]=static_cast<char>(a.data[i+1]); out[n]='\0'; return out[0]=='/'; }
bool same(const char*a,const char*b){if(!a||!b)return false;while(*a&&*a==*b){++a;++b;}return *a==*b;}
unix_file* find(const char* n){for(auto& e:names)if(e.used&&e.listener&&same(e.name,n))return e.listener;return nullptr;}
}
long create_unix_socket(mm::shptr<open_file>& out){auto f=mm::share<unix_file>(); if(!f)return -ENOMEM; out=mm::share<open_file>(f,0); return out?-0:-ENOMEM;}
long unix_bind(open_file& o,const astia_sockaddr& a){auto* f=static_cast<unix_file*>(o.backing());char n[path_max];if(!f||!path(a,n))return -EINVAL;core::spinlock_guard g(names_lock);if(find(n))return -EADDRINUSE;for(auto&e:names)if(!e.used){e.used=true;e.listener=f;for(u32 i=0;i<path_max;++i){e.name[i]=n[i];if(!n[i])break;}f->mark_bound();return 0;}return -ENFILE;}
long unix_listen(open_file& o,u32 b){auto*f=static_cast<unix_file*>(o.backing());if(!f||f->kind()!=unix_file::state::fresh)return -EINVAL;f->make_listener(b>max_backlog?max_backlog:b?b:1);return 0;}
long unix_connect(open_file& o,const astia_sockaddr&a){auto*c=static_cast<unix_file*>(o.backing());char n[path_max];if(!c||!path(a,n))return -EINVAL;core::spinlock_guard g(names_lock);auto*l=find(n);if(!l||l->kind()!=unix_file::state::listener)return -ECONNREFUSED;auto ch=mm::share<channel>();auto s=mm::share<unix_file>();if(!ch||!s)return -ENOMEM;/* peer objects are kept alive by their open descriptions */c->attach(ch,0,s.get());s->attach(ch,1,c);auto so=mm::share<open_file>(s,0);if(!so||!l->enqueue(so))return -ECONNREFUSED;return 0;}
long unix_accept(open_file&o,mm::shptr<open_file>&out){auto*l=static_cast<unix_file*>(o.backing());if(!l||l->kind()!=unix_file::state::listener)return -EINVAL;out=l->dequeue();return out?0:0;}
}
