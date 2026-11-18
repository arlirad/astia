#pragma once

#include "../tty.hpp"
#include <astia/fs.hpp>
#include <astia/mm/shptr.hpp>

namespace astia::dev {

    class pty final : public tty {
        public:
        explicit pty(u32 index);

        long read(void* buffer, u32 length, u64 offset) override;
        long write(const void* buffer, u32 length, u64 offset) override;
        long submit(io::request& request) override;
        u32  poll(u32 events, u32 flags) override;
        long ioctl(u64 request, void* argp, usize size) override;
        long acquire_controlling_terminal(i32 group) override;
        void duplicate(u32 flags);
        void close(u32 flags);

        u32 index() const { return index_; }
        bool open_slave();
        void bind_master(fs::file* master);
        void master_closed();
        bool master_open() const;
        bool slave_open() const;
        void bind_slave(fs::file* slave);
        long master_read(void* buffer, u32 length);
        long master_write(const void* buffer, u32 length);
        long master_submit(io::request& request);
        u32  master_poll(u32 events) const;
        long master_ioctl(u64 request, void* argp, usize size);
        void master_duplicate();
        void master_close();

        protected:
        bool tty_putc(u8 byte) override;
        void tty_ready() override;
        long tty_speed(speed_t speed) override;

        private:
        void release_if_unused();
        bool master_queue_empty() const;

        static constexpr u32 queue_capacity = 4096;
        u32              index_ = 0;
        mutable core::spinlock pty_lock_;
        u8               master_queue_[queue_capacity]{};
        u32              master_head_ = 0;
        u32              master_tail_ = 0;
        u32              master_refs_ = 1;
        u32              slave_refs_ = 0;
        bool             master_open_ = true;
        bool             locked_ = true;
        fs::file*        master_file_ = nullptr;
        fs::file*        slave_file_ = nullptr;
        struct winsize   winsize_{24, 80, 0, 0};
    };

    mm::shptr<fs::open_file> pty_open_master(u32 flags);
    mm::shptr<fs::open_file> pty_open_slave(const char* path, u32 flags);
    bool pty_node_exists(const char* path);
    bool pty_node_is_directory(const char* path);
    bool pty_node_index(const char* path, u32* index);
    void pty_release_slot(u32 index, pty* object);

} // namespace astia::dev
