#include "arena.hpp"
#include "test.hpp"

#ifdef RENAME_NOREPLACE
#undef RENAME_NOREPLACE
#endif
#ifdef RENAME_EXCHANGE
#undef RENAME_EXCHANGE
#endif

#include <astia/api/errno.h>
#include <astia/fs.hpp>
#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/heap.hpp>

#include "../../modules/devfs/devfs.hpp"

#include <cstring>

alignas(16) unsigned char device_registry_heap_storage[256 * 1024];

namespace astia::proc {
    mm::shptr<fs::open_file> controlling_terminal() {
        return {};
    }
} // namespace astia::proc

namespace {
    using astia::u32;
    using astia::u64;
    using astia::u8;

    class probe_file final : public astia::fs::file,
                             public astia::fs::readable,
                             public astia::fs::writable,
                             public astia::fs::pollable,
                             public astia::fs::ioctlable {
        public:
        astia::optional<astia::fs::readable&> as_readable() override {
            return *this;
        }

        astia::optional<astia::fs::writable&> as_writable() override {
            return *this;
        }

        astia::optional<astia::fs::pollable&> as_pollable() override {
            return *this;
        }

        astia::optional<astia::fs::ioctlable&> as_ioctlable() override {
            return *this;
        }

        astia::core::task<astia::i64> readv(astia::span<const astia::fs::buffer>, u64,
                                            astia::core::cltoken) override {
            co_return -EINVAL;
        }

        astia::core::task<astia::i64> writev(astia::span<const astia::fs::buffer> buffers,
                                             u64, astia::core::cltoken) override {
            co_return static_cast<astia::i64>(buffers.size());
        }

        u32 poll(u32, u32) override {
            return 0;
        }

        astia::core::task<astia::i64> ioctl(u64, void*, astia::usize,
                                            astia::core::cltoken) override {
            co_return 0;
        }

        astia::fs::file_mode mode() const override {
            return astia::fs::file_mode(astia::fs::file_type::character,
                                        astia::fs::default_device_permissions);
        }
    };

    astia::fs::filesystems::devfs       devfs_type;
    astia::fs::filesystems::devfs_mount devfs_mount;
    probe_file                          node_file;
    probe_file                          pending_null;
    bool                                initialized = false;
    bool                                devfs_ready = false;

    bool no_mapping(u64, u64) {
        return true;
    }

    void initialize_runtime() {
        if (initialized)
            return;

        astia_test::map_arena();

        astia::mm::memory_map map;

        map.add(astia_test::arena_base, astia_test::arena_size,
                astia::mm::memory_type::usable);
        astia::mm::kernel_frames().init(map);
        astia::mm::heap::configure(device_registry_heap_storage,
                                   sizeof device_registry_heap_storage, no_mapping);
        ASTIA_CHECK(astia::mm::heap::init());
        ASTIA_CHECK(astia::fs::init());

        initialized = true;
    }

    void mount_devfs() {
        if (devfs_ready)
            return;

        devfs_type.attach(&devfs_mount);
        ASTIA_CHECK(astia::fs::register_type(&devfs_type) == 0);
        ASTIA_CHECK(astia::fs::mount_type("devfs", "/dev") == 0);
        ASTIA_CHECK(astia::fs::activate_device_namespace() == 0);

        devfs_ready = true;
    }

    astia::fs::device_node_descriptor descriptor(const char*      name,
                                                 astia::fs::file* file) {
        astia::fs::device_node_descriptor value{};

        value.name        = name;
        value.node        = astia::mm::shptr<astia::fs::file>::borrowed(file);
        value.type        = astia::fs::file_type::character;
        value.permissions = astia::fs::default_device_permissions;

        return value;
    }

    long                          quiesce_calls = 0;
    bool                          quiesce_ready = false;
    astia::fs::device_node_handle quiesce_handle{};
    long                          reentrant_unregister = 0;

    long quiesce(void*, u64) {
        ++quiesce_calls;

        if (quiesce_handle.valid())
            reentrant_unregister = astia::fs::unregister_device_node(quiesce_handle);

        return quiesce_ready ? 0 : -EBUSY;
    }

    u32 retain_calls  = 0;
    u32 release_calls = 0;

    void retain(void*) {
        ++retain_calls;
    }

    void release(void*) {
        ++release_calls;
    }
} // namespace

ASTIA_TEST(device_registry_rejects_malformed_descriptors) {
    initialize_runtime();

    astia::fs::device_node_handle handle{};
    auto                          value = descriptor("probe", &node_file);

    value.version = 0;

    ASTIA_CHECK(astia::fs::register_device_node(value, handle) == -EINVAL);

    value.version = astia::fs::device_node_descriptor_version;

    value.name = "/probe";

    ASTIA_CHECK(astia::fs::register_device_node(value, handle) == -EINVAL);

    value.name = "probe/";

    ASTIA_CHECK(astia::fs::register_device_node(value, handle) == -EINVAL);

    value.name = "probe//child";

    ASTIA_CHECK(astia::fs::register_device_node(value, handle) == -EINVAL);

    value.name = "probe/../child";

    ASTIA_CHECK(astia::fs::register_device_node(value, handle) == -EINVAL);

    value.name = "probe";
    value.node = {};

    ASTIA_CHECK(astia::fs::register_device_node(value, handle) == -EINVAL);

    value      = descriptor("probe", &node_file);
    value.type = astia::fs::file_type::regular;

    ASTIA_CHECK(astia::fs::register_device_node(value, handle) == -EINVAL);

    const char* aliases[] = {"probe", "other"};

    value         = descriptor("probe", &node_file);
    value.aliases = astia::span<const char* const>(aliases, 2);

    ASTIA_CHECK(astia::fs::register_device_node(value, handle) == -EINVAL);
}

ASTIA_TEST(device_registry_pending_before_activation_is_not_visible) {
    initialize_runtime();

    astia::fs::device_node_handle handle{};

    ASTIA_CHECK(
        astia::fs::register_device_node(descriptor("null", &pending_null), handle) == 0);

    astia::fs::device_node_info info{};

    ASTIA_CHECK(astia::fs::lookup_device_node("null", info) == -ENODEV);
    ASTIA_CHECK(!astia::fs::device_namespace_ready());

    astia::mm::shptr<astia::fs::file>              file;
    astia::mm::shptr<astia::fs::device_node_lease> lease;

    ASTIA_CHECK(astia::fs::open_device_node("null", file, lease) == -ENODEV);
    ASTIA_CHECK(!astia::fs::node_mode("/dev/null").exists());
    ASTIA_CHECK(!astia::fs::open("/dev/null"));
}

ASTIA_TEST(device_registry_collisions_and_aliases) {
    initialize_runtime();

    const char*                   aliases[] = {"alias-null"};
    astia::fs::device_node_handle handle{};
    auto                          value = descriptor("alias-target", &node_file);

    value.aliases = astia::span<const char* const>(aliases, 1);

    ASTIA_CHECK(astia::fs::register_device_node(value, handle) == 0);

    astia::fs::device_node_handle duplicate{};

    ASTIA_CHECK(astia::fs::register_device_node(descriptor("alias-target", &node_file),
                                                duplicate) == -EEXIST);
    ASTIA_CHECK(astia::fs::register_device_node(descriptor("alias-null", &node_file),
                                                duplicate) == -EEXIST);
}

ASTIA_TEST(device_registry_activation_materialises_pending_nodes) {
    initialize_runtime();
    mount_devfs();

    astia::fs::device_node_info info{};

    ASTIA_CHECK(astia::fs::lookup_device_node("null", info) == 0);
    ASTIA_CHECK(info.type == astia::fs::file_type::character);
    ASTIA_CHECK(astia::fs::node_type("/dev/null") == astia::fs::directory_chr);
    ASTIA_CHECK(astia::fs::node_mode("/dev/null").permissions().raw() == 0666);

    auto opened = astia::fs::open("/dev/null");

    ASTIA_CHECK(opened);
}

ASTIA_TEST(device_registry_quiesce_retry_open_lease_and_stale_handle) {
    initialize_runtime();
    mount_devfs();

    const char*                   alias[] = {"quiesce-alias"};
    astia::fs::device_node_handle handle{};
    auto                          value = descriptor("quiesce", &node_file);

    value.aliases         = astia::span<const char* const>(alias, 1);
    value.quiesce         = quiesce;
    value.quiesce_context = nullptr;

    quiesce_calls        = 0;
    quiesce_ready        = false;
    reentrant_unregister = 0;

    ASTIA_CHECK(astia::fs::register_device_node(value, handle) == 0);

    quiesce_handle = handle;

    auto opened = astia::fs::open("/dev/quiesce-alias");

    ASTIA_CHECK(opened);

    astia::fs::device_node_info info{};

    ASTIA_CHECK(astia::fs::lookup_device_node("quiesce", info) == 0);
    ASTIA_CHECK(info.open_count == 1);

    ASTIA_CHECK(astia::fs::unregister_device_node(handle) == -EBUSY);
    ASTIA_CHECK(quiesce_calls == 1);
    ASTIA_CHECK(reentrant_unregister == -EBUSY);
    ASTIA_CHECK(astia::fs::lookup_device_node("quiesce", info) == -EBUSY);
    ASTIA_CHECK(!astia::fs::open("/dev/quiesce"));

    quiesce_ready = true;

    ASTIA_CHECK(astia::fs::unregister_device_node(handle) == 0);
    ASTIA_CHECK(quiesce_calls == 2);
    ASTIA_CHECK(astia::fs::lookup_device_node("quiesce", info) == -ENOENT);
    ASTIA_CHECK(astia::fs::unregister_device_node(handle) == -ENOENT);
    ASTIA_CHECK(!astia::fs::node_mode("/dev/quiesce").exists());

    quiesce_handle = {};

    astia::fs::device_node_handle reused{};

    ASTIA_CHECK(
        astia::fs::register_device_node(descriptor("quiesce", &node_file), reused) == 0);
    ASTIA_CHECK(astia::fs::unregister_device_node(handle) == -ENOENT);
    ASTIA_CHECK(astia::fs::unregister_device_node(reused) == 0);

    opened = {};
}

ASTIA_TEST(device_registry_lifetime_token_is_balanced) {
    initialize_runtime();

    astia::fs::device_node_handle handle{};
    auto                          value = descriptor("lifetime", &node_file);

    value.lifetime.retain  = retain;
    value.lifetime.release = release;

    retain_calls  = 0;
    release_calls = 0;

    ASTIA_CHECK(astia::fs::register_device_node(value, handle) == 0);
    ASTIA_CHECK(retain_calls == 1);
    ASTIA_CHECK(release_calls == 0);

    astia::mm::shptr<astia::fs::file>              failed_open;
    astia::mm::shptr<astia::fs::device_node_lease> failed_lease;

    astia_test::fail_next_allocation();

    ASTIA_CHECK(astia::fs::open_device_node("lifetime", failed_open, failed_lease) ==
                -ENOMEM);
    ASTIA_CHECK(!failed_open);
    ASTIA_CHECK(!failed_lease);

    ASTIA_CHECK(astia::fs::unregister_device_node(handle) == 0);
    ASTIA_CHECK(release_calls == 1);
}

ASTIA_TEST(device_registry_capacity_is_bounded_and_reusable) {
    initialize_runtime();

    astia::fs::device_node_handle handles[astia::fs::max_device_nodes]{};

    char names[astia::fs::max_device_nodes][16]{};
    u32  held = 0;
    long last = 0;

    for (u32 i = 0; i < astia::fs::max_device_nodes; ++i) {
        names[i][0] = 'c';
        names[i][1] = 'a';
        names[i][2] = 'p';
        names[i][3] = static_cast<char>('a' + i / 10);
        names[i][4] = static_cast<char>('0' + i % 10);
        names[i][5] = '\0';

        last = astia::fs::register_device_node(descriptor(names[i], &node_file),
                                               handles[held]);
        if (last != 0)
            break;

        ++held;
    }

    ASTIA_CHECK(held != 0);
    ASTIA_CHECK(last == -ENFILE || last == -ENOSPC);

    ASTIA_CHECK(astia::fs::unregister_device_node(handles[0]) == 0);
    ASTIA_CHECK(astia::fs::register_device_node(descriptor("capacity-reuse", &node_file),
                                                handles[0]) == 0);
    ASTIA_CHECK(astia::fs::unregister_device_node(handles[0]) == 0);
}

ASTIA_TEST(devfs_mounts_special_and_pending_pseudo_nodes) {
    initialize_runtime();
    mount_devfs();

    ASTIA_CHECK(astia::fs::node_type("/dev/null") == astia::fs::directory_chr);
    ASTIA_CHECK(astia::fs::node_type("/dev/tty") == astia::fs::directory_chr);
    ASTIA_CHECK(astia::fs::node_type("/dev/ptmx") == astia::fs::directory_chr);
    ASTIA_CHECK(astia::fs::node_type("/dev/pts") == astia::fs::directory_dir);
    ASTIA_CHECK(astia::fs::node_mode("/dev/tty").permissions().raw() == 0666);
}
