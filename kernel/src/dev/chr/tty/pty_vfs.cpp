#include "pty.hpp"

#include "../../../sys/aio.hpp"
#include <astia/api/errno.h>
#include <astia/mm/share.hpp>

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

        class pty_slave_file final : public fs::file {
            public:
            explicit pty_slave_file(const mm::shptr<pty>& owner) : owner_(owner) {}

            long read(void* buffer, u32 length, u64 offset) override {
                return owner_->read(buffer, length, offset);
            }
            long write(const void* buffer, u32 length, u64 offset) override {
                return owner_->write(buffer, length, offset);
            }
            long submit(io::request& request) override {
                return owner_->submit(request);
            }
            u32 poll(u32 events, u32 flags) override {
                return owner_->poll(events, flags);
            }
            long ioctl(u64 request, void* argp, usize size) override {
                return owner_->ioctl(request, argp, size);
            }
            long acquire_controlling_terminal(i32 group) override {
                return owner_->acquire_controlling_terminal(group);
            }
            fs::file_mode mode() const override {
                return fs::file_mode(fs::file_type::character,
                                     fs::default_device_permissions);
            }
            void duplicate(u32 flags) override { owner_->duplicate(flags); }
            void close(u32 flags) override { owner_->close(flags); }

            private:
            mm::shptr<pty> owner_;
        };

        class pty_master_file final : public fs::file {
            public:
            explicit pty_master_file(const mm::shptr<pty>& owner) : owner_(owner) {}
            ~pty_master_file() override {
                if (refs_ != 0)
                    owner_->master_close();
            }

            long read(void* buffer, u32 length, u64) override {
                return owner_->master_read(buffer, length);
            }
            long write(const void* buffer, u32 length, u64) override {
                return owner_->master_write(buffer, length);
            }
            long submit(io::request& request) override {
                return owner_->master_submit(request);
            }
            u32 poll(u32 events, u32) override { return owner_->master_poll(events); }
            long ioctl(u64 request, void* argp, usize size) override {
                return owner_->master_ioctl(request, argp, size);
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
            u32 refs_ = 1;
        };

        struct pty_slot {
            bool                     reserved = false;
            mm::shptr<pty>           object;
            mm::shptr<pty_slave_file> slave_file;
        };

        pty_slot       slots[max_ptys];
        core::spinlock slots_lock;
    }

    void pty_release_slot(u32 index, pty* object) {
        core::spinlock_guard guard(slots_lock);
        if (index < max_ptys && slots[index].object.get() == object) {
            slots[index].slave_file = {};
            slots[index].object = {};
        }
    }

    mm::shptr<fs::open_file> pty_open_master(u32 flags) {
        u32 index = max_ptys;
        {
            core::spinlock_guard guard(slots_lock);
            for (u32 i = 0; i < max_ptys; ++i) {
                if (!slots[i].reserved && !slots[i].object) {
                    slots[i].reserved = true;
                    index = i;
                    break;
                }
            }
        }
        if (index == max_ptys)
            return {};
        auto object = mm::share<pty>(index);
        auto slave_file = object ? mm::share<pty_slave_file>(object)
                                 : mm::shptr<pty_slave_file>{};
        if (!object || !slave_file) {
            core::spinlock_guard guard(slots_lock);
            slots[index].reserved = false;
            return {};
        }
        {
            core::spinlock_guard guard(slots_lock);
            slots[index].object = object;
            slots[index].slave_file = slave_file;
            slots[index].reserved = false;
        }
        object->bind_slave(slave_file.get());
        auto master = mm::share<pty_master_file>(object);
        if (!master) {
            object->master_close();
            return {};
        }
        object->bind_master(master.get());
        auto result = mm::share<fs::open_file>(master, flags);
        if (!result)
            master->close(0);
        return result;
    }

    mm::shptr<fs::open_file> pty_open_slave(const char* path, u32 flags) {
        u32 index;
        if (!parse_index(path, &index) || index >= max_ptys)
            return {};
        mm::shptr<pty> object;
        mm::shptr<pty_slave_file> slave_file;
        {
            core::spinlock_guard guard(slots_lock);
            object = slots[index].object;
            slave_file = slots[index].slave_file;
        }
        if (!object || !slave_file || !object->open_slave())
            return {};
        auto result = mm::share<fs::open_file>(slave_file, flags);
        if (!result)
            object->close(0);
        return result;
    }

    bool pty_node_index(const char* path, u32* index) { return parse_index(path, index); }

    bool pty_node_exists(const char* path) {
        u32 index;
        if (!parse_index(path, &index) || index >= max_ptys)
            return false;
        core::spinlock_guard guard(slots_lock);
        return static_cast<bool>(slots[index].object);
    }

    bool pty_node_is_directory(const char* path) {
        return path != nullptr && path[0] == '/' && path[1] == 'p' && path[2] == 't' &&
               path[3] == 's' && path[4] == '\0';
    }
} // namespace astia::dev
