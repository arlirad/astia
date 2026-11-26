#include <astia/api/errno.h>
#include <astia/mm/share.hpp>

#include "../../../sys/aio.hpp"
#include "pty.hpp"

namespace astia::dev {
    namespace {
        constexpr u32 max_ptys = 16;

        bool parse_index(const char* path, u32* index) {
            if (path == nullptr || index == nullptr || path[0] != '/' || path[1] != 'p' ||
                path[2] != 't' || path[3] != 's' || path[4] != '/' || path[5] == '\0')
                return false;

            u32 value = 0;

            for (u32 i = 5; path[i] != '\0'; ++i) {
                if (path[i] < '0' || path[i] > '9' || value > 0xffffffffu / 10)
                    return false;

                value = value * 10 + static_cast<u32>(path[i] - '0');
            }

            *index = value;

            return true;
        }

        class pty_slave_file final : public fs::file,
                                     public fs::readable,
                                     public fs::writable,
                                     public fs::pollable,
                                     public fs::ioctlable {
            public:
            explicit pty_slave_file(const mm::shptr<pty>& owner) : owner_(owner) {}

            optional<fs::readable&> as_readable() override {
                return *this;
            }

            optional<fs::writable&> as_writable() override {
                return *this;
            }

            optional<fs::pollable&> as_pollable() override {
                return *this;
            }

            optional<fs::ioctlable&> as_ioctlable() override {
                return *this;
            }

            core::task<i64> readv(span<const fs::buffer> buffers, u64 offset,
                                  core::cltoken token) override {
                return submit_readv(buffers, offset, token);
            }

            core::task<i64> writev(span<const fs::buffer> buffers, u64 offset,
                                   core::cltoken token) override {
                return submit_writev(buffers, offset, token);
            }

            long submit(io::request& request) override {
                return owner_->submit(request);
            }

            u32 poll(u32 events, u32 flags) override {
                return owner_->poll(events, flags);
            }

            core::task<i64> ioctl(u64 request, void* argp, usize size,
                                  core::cltoken) override {
                co_return owner_->ioctl(request, argp, size);
            }

            long acquire_controlling_terminal(i32 group) override {
                return owner_->acquire_controlling_terminal(group);
            }

            fs::file_mode mode() const override {
                return fs::file_mode(fs::file_type::character,
                                     fs::default_device_permissions);
            }

            void duplicate(u32 flags) override {
                owner_->duplicate(flags);
            }

            void close(u32 flags) override {
                owner_->close(flags);
            }

            private:
            mm::shptr<pty> owner_;
        };

        class pty_master_file final : public fs::file,
                                      public fs::readable,
                                      public fs::writable,
                                      public fs::pollable,
                                      public fs::ioctlable {
            public:
            explicit pty_master_file(const mm::shptr<pty>& owner) : owner_(owner) {}

            ~pty_master_file() override {
                if (refs_ != 0)
                    owner_->master_close();
            }

            optional<fs::readable&> as_readable() override {
                return *this;
            }

            optional<fs::writable&> as_writable() override {
                return *this;
            }

            optional<fs::pollable&> as_pollable() override {
                return *this;
            }

            optional<fs::ioctlable&> as_ioctlable() override {
                return *this;
            }

            core::task<i64> readv(span<const fs::buffer> buffers, u64 offset,
                                  core::cltoken token) override {
                return submit_readv(buffers, offset, token);
            }

            core::task<i64> writev(span<const fs::buffer> buffers, u64 offset,
                                   core::cltoken token) override {
                return submit_writev(buffers, offset, token);
            }

            long submit(io::request& request) override {
                return owner_->master_submit(request);
            }

            u32 poll(u32 events, u32) override {
                return owner_->master_poll(events);
            }

            core::task<i64> ioctl(u64 request, void* argp, usize size,
                                  core::cltoken) override {
                co_return owner_->master_ioctl(request, argp, size);
            }

            fs::file_mode mode() const override {
                return fs::file_mode(fs::file_type::character,
                                     fs::default_device_permissions);
            }

            void duplicate(u32) override {
                ++refs_;
                owner_->master_duplicate();
            }

            void close(u32) override {
                if (refs_ != 0 && --refs_ == 0)
                    owner_->master_close();
            }

            private:
            mm::shptr<pty> owner_;
            u32            refs_ = 1;
        };

        struct pty_slot {
            bool                      reserved = false;
            mm::shptr<pty>            instance;
            mm::shptr<pty_slave_file> slave_file;
        };

        pty_slot       slots[max_ptys];
        core::spinlock slots_lock;
    }

    void pty_release_slot(u32 index, pty* instance) {
        core::spinlock_guard guard(slots_lock);

        if (index < max_ptys && slots[index].instance.get() == instance) {
            slots[index].slave_file = {};
            slots[index].instance   = {};
        }
    }

    mm::shptr<fs::open_file> pty_open_master(u32 flags) {
        u32 index = max_ptys;

        {
            core::spinlock_guard guard(slots_lock);

            for (u32 i = 0; i < max_ptys; ++i) {
                if (!slots[i].reserved && !slots[i].instance) {
                    slots[i].reserved = true;
                    index             = i;

                    break;
                }
            }
        }

        if (index == max_ptys)
            return {};

        auto instance = mm::share<pty>(index);
        auto slave_file =
            instance ? mm::share<pty_slave_file>(instance) : mm::shptr<pty_slave_file>{};

        if (!instance || !slave_file) {
            core::spinlock_guard guard(slots_lock);

            slots[index].reserved = false;

            return {};
        }

        {
            core::spinlock_guard guard(slots_lock);

            slots[index].instance   = instance;
            slots[index].slave_file = slave_file;
            slots[index].reserved   = false;
        }

        instance->bind_slave(slave_file.get());

        auto master = mm::share<pty_master_file>(instance);
        if (!master) {
            instance->master_close();

            return {};
        }

        instance->bind_master(master.get());

        auto result = mm::share<fs::open_file>(master, flags);
        if (!result)
            master->close(0);

        return result;
    }

    mm::shptr<fs::open_file> pty_open_slave(const char* path, u32 flags) {
        u32 index;

        if (!parse_index(path, &index) || index >= max_ptys)
            return {};

        mm::shptr<pty>            instance;
        mm::shptr<pty_slave_file> slave_file;

        {
            core::spinlock_guard guard(slots_lock);

            instance   = slots[index].instance;
            slave_file = slots[index].slave_file;
        }

        if (!instance || !slave_file || !instance->open_slave())
            return {};

        auto result = mm::share<fs::open_file>(slave_file, flags);
        if (!result)
            instance->close(0);

        return result;
    }

    bool pty_node_index(const char* path, u32* index) {
        return parse_index(path, index);
    }

    bool pty_node_exists(const char* path) {
        u32 index;

        if (!parse_index(path, &index) || index >= max_ptys)
            return false;

        core::spinlock_guard guard(slots_lock);

        return static_cast<bool>(slots[index].instance);
    }

    bool pty_node_is_directory(const char* path) {
        return path != nullptr && path[0] == '/' && path[1] == 'p' && path[2] == 't' &&
               path[3] == 's' && path[4] == '\0';
    }
} // namespace astia::dev
