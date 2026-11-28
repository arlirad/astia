#pragma once

#include <astia/api/aio.h>
#include <astia/common.hpp>
#include <astia/fs.hpp>
#include <astia/mm/shptr.hpp>

namespace astia::io::aio::ring {
    struct ring_slot;
}

namespace astia::io::aio::test_support {
    struct touch_record {
        char                path[128]{};
        fs::file_timestamps times{};
        bool                follow_final = true;
    };

    struct cq_capture {
        astia_rings           ring{};
        mutable astia_cq_ring cq{};
        astia_cqe             cqes[64]{};

        cq_capture();

        u32              count() const;
        const astia_cqe* at(u32 index) const;
        i32              result(u32 index) const;
        u64              user_data(u32 index) const;
        void             consume(u32 count);
    };

    struct chown_record {
        bool  called = false;
        uid_t uid{};
        gid_t gid{};
        bool  follow_final = true;
    };

    struct chmod_record {
        bool                 called = false;
        char                 path[fs::path_max]{};
        fs::file_permissions permissions{};
    };

    struct creation_record {
        bool                    called = false;
        char                    path[fs::path_max]{};
        fs::creation_attributes attributes{};
    };

    struct symlink_record {
        char target[fs::path_max]{};
        char path[fs::path_max]{};
        bool called = false;
    };

    struct remove_record {
        bool called = false;
        char path[fs::path_max]{};
        bool directory = false;
    };

    void reset_environment();
    void install_current_process();
    void clear_current_process();

    const chown_record&    last_chown_path();
    const chmod_record&    last_chmod_path();
    const creation_record& last_creation();
    const symlink_record&  last_symlink();
    const remove_record&   last_remove_path();
    bool                   last_resolve_follow_final();
    void                   set_readlink_target(const char* target);

    i32  install_descriptor(const mm::shptr<fs::open_file>& file, u32 flags = 0);
    bool descriptor_present(i32 fd);
    u32  descriptor_flags(i32 fd);
    void clear_descriptor(i32 fd);

    void set_open_file(const mm::shptr<fs::open_file>& file);
    void set_pipe_files(const mm::shptr<fs::open_file>& read_end,
                        const mm::shptr<fs::open_file>& write_end);
    void set_socket_file(const mm::shptr<fs::open_file>& file);
    void set_socketpair_files(const mm::shptr<fs::open_file>& first,
                              const mm::shptr<fs::open_file>& second);
    void set_node_mode(fs::file_mode mode);
    void set_node_mode(const char* path, fs::file_mode mode);
    void set_node_owner(const char* path, uid_t uid, gid_t gid);
    void set_node_identity(const char* path, u32 device, u64 inode);
    void set_directory_entry_inode(u64 inode);
    void set_process_credentials(uid_t uid, gid_t gid);
    void set_process_umask(u32 mask);
    void set_process_root(const char* root);
    void set_process_cwd(const char* cwd);
    void set_memory_allowed(bool allowed);
    void set_timer(u64 hz, u64 ticks);
    void add_timer_ticks(u64 ticks);
    void release_parked(ring::ring_slot& slot);

    bool                touch_called();
    const touch_record& last_touch();
    void                clear_touch_record();
} // namespace astia::io::aio::test_support
