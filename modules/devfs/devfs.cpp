#include "devfs.hpp"

#include <astia/api/errno.h>
#include <astia/dev/pty.hpp>
#include <astia/fs/device_node.hpp>
#include <astia/mm/share.hpp>
#include <astia/proc/terminal.hpp>
#include <astia/strings.hpp>

#ifndef ASTIA_DEVFS_HOST_TEST
#include <astia/module.hpp>
#endif

namespace astia::fs::filesystems {
    namespace {
        const char* node_name(const char* path) {
            if (path == nullptr)
                return nullptr;

            return path[0] == '/' ? path + 1 : path;
        }

        bool permission_denied(file_permissions permissions, u32 flags) {
            if ((flags & ASTIA_AIO_O_RDWR) == ASTIA_AIO_O_RDWR)
                return !permissions.other_allows(permission_bits::read) ||
                       !permissions.other_allows(permission_bits::write);

            if ((flags & ASTIA_AIO_O_RDWR) != ASTIA_AIO_O_RDWR &&
                (flags & ASTIA_AIO_O_WRONLY) != 0)
                return !permissions.other_allows(permission_bits::write);

            if ((flags & ASTIA_AIO_O_ACCMODE) == ASTIA_AIO_O_RDONLY)
                return !permissions.other_allows(permission_bits::read);

            return false;
        }
    } // namespace

    mm::shptr<open_file> devfs_mount::open(const char* path, u32 flags) {
        const char* name = node_name(path);
        if (name == nullptr)
            return {};

        if (string_equal(name, "ptmx"))
            return dev::pty_open_master(flags);

        if (dev::pty_node_is_directory(name))
            return {};

        if (dev::pty_node_exists(name))
            return dev::pty_open_slave(name, flags);

        if (string_equal(name, "tty")) {
            mm::shptr<open_file> terminal = proc::controlling_terminal();
            if (!terminal)
                return {};

            auto result = mm::share<open_file>(*terminal);
            if (result)
                result->set_status_flags(flags);

            return result;
        }

        device_node_info info{};

        if (fs::lookup_device_node(name, info) < 0)
            return {};

        if (permission_denied(info.permissions, flags))
            return {};

        mm::shptr<file>              target;
        mm::shptr<device_node_lease> lease;

        if (fs::open_device_node(name, target, lease) < 0)
            return {};

        auto result = mm::share<open_file>(target, flags);
        if (result)
            result->attach_node_lease(static_cast<mm::shptr<device_node_lease>&&>(lease));

        return result;
    }

    file_type devfs_mount::node_type(const char* path) const {
        const char* name = node_name(path);
        if (name == nullptr)
            return directory_unknown;

        if (string_equal(name, "tty"))
            return directory_chr;

        if (dev::pty_node_is_directory(name))
            return directory_dir;

        if (dev::pty_node_exists(name) || string_equal(name, "ptmx"))
            return directory_chr;

        device_node_info info{};

        if (fs::lookup_device_node(name, info) < 0)
            return directory_unknown;

        return info.type;
    }

    file_mode devfs_mount::node_mode(const char* path) const {
        const char* name = node_name(path);
        if (name == nullptr)
            return {};

        if (string_equal(name, "tty"))
            return file_mode(file_type::character, default_device_permissions);

        if (dev::pty_node_is_directory(name))
            return file_mode(file_type::directory, default_directory_permissions);

        if (dev::pty_node_exists(name) || string_equal(name, "ptmx"))
            return file_mode(file_type::character, default_device_permissions);

        device_node_info info{};

        if (fs::lookup_device_node(name, info) < 0)
            return {};

        return file_mode(info.type, info.permissions);
    }

    uid_t devfs_mount::node_uid(const char* path) const {
        device_node_info info{};

        return fs::lookup_device_node(node_name(path), info) == 0 ? info.uid : uid_t{};
    }

    gid_t devfs_mount::node_gid(const char* path) const {
        device_node_info info{};

        return fs::lookup_device_node(node_name(path), info) == 0 ? info.gid : gid_t{};
    }

    fs::mount* devfs::mount(const char* source) const {
        (void) source;

        return _instance;
    }
} // namespace astia::fs::filesystems

#ifndef ASTIA_DEVFS_HOST_TEST
namespace {
    astia::fs::filesystems::devfs       devfs_type_instance;
    astia::fs::filesystems::devfs_mount devfs_mount_instance;
} // namespace

int module_main() {
    devfs_type_instance.attach(&devfs_mount_instance);

    if (astia::fs::register_type(&devfs_type_instance) < 0)
        return -1;

    if (astia::fs::mount_type("devfs", "/dev") < 0)
        return -1;

    if (astia::fs::activate_device_namespace() < 0)
        return -1;

    astia::module::log("devfs", "mounted /dev");

    return 0;
}
#endif
