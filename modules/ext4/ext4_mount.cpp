#include <astia/api/errno.h>
#include <astia/strings.hpp>

#include "ext4.hpp"

namespace astia::ext4 {
    namespace {
        bool copy_name(char* destination, const u8* source, u8 length, u32 capacity) {
            if (static_cast<u32>(length) + 1 > capacity)
                return false;

            for (u32 i = 0; i < length; ++i) {
                destination[i] = static_cast<char>(source[i]);
            }

            destination[length] = '\0';

            return true;
        }

        fs::file_type convert_type(ext4::node_type type) {
            switch (type) {
            case ext4::node_type::regular:
                return fs::directory_reg;
            case ext4::node_type::directory:
                return fs::directory_dir;
            case ext4::node_type::symlink:
                return fs::directory_lnk;
            case ext4::node_type::character:
                return fs::directory_chr;
            case ext4::node_type::fifo:
                return fs::directory_fifo;
            case ext4::node_type::socket:
                return fs::directory_sock;
            default:
                return fs::directory_unknown;
            }
        }

        struct lookup_context {
            const char* name;
            u32         inode = 0;
            bool        found = false;
        };

        bool lookup_entry(void* opaque, const ext4::directory_entry& entry) {
            auto& context = *static_cast<lookup_context*>(opaque);

            if (entry.name_length == 0)
                return true;

            u32 length = 0;

            while (context.name[length] != '\0') {
                ++length;
            }

            if (length == entry.name_length) {
                bool same = true;

                for (u32 i = 0; i < length; ++i) {
                    if (context.name[i] != static_cast<char>(entry.name[i]))
                        same = false;
                }

                if (same) {
                    context.inode = entry.inode;
                    context.found = true;

                    return false;
                }
            }

            return true;
        }

        struct directory_context {
            u64        wanted;
            u64        ordinal = 0;
            char*      name;
            u32        capacity;
            fs::file_type* type;
            u64*       inode;
            long       result = 0;
        };

        bool enumerate_entry(void* opaque, const ext4::directory_entry& entry) {
            auto& context = *static_cast<directory_context*>(opaque);

            if (entry.inode == 0)
                return true;

            if (context.ordinal++ != context.wanted)
                return true;

            if (!copy_name(context.name, entry.name, entry.name_length,
                           context.capacity)) {
                context.result = -ENAMETOOLONG;

                return false;
            }

            *context.type  = convert_type(entry.type);
            context.result = 1;

            if (context.inode != nullptr)
                *context.inode = entry.inode;

            return false;
        }
    }

    bool mount::read_source(void* context, u64 offset, void* buffer, usz length) {
        return static_cast<mount*>(context)->source_read(offset, buffer, length);
    }

    bool mount::source_read(u64 offset, void* buffer, usz length) const {
        if (!_source || (buffer == nullptr && length != 0))
            return false;

        const long size               = _source->size();
        const u32  source_sector_size = _source->block_size();

        if (source_sector_size == 0 || source_sector_size > 4096 || size < 0 ||
            offset > static_cast<u64>(size) || length > static_cast<u64>(size) - offset)
            return false;

        auto* output = static_cast<u8*>(buffer);

        while (length != 0) {
            const u64 sector = offset - offset % source_sector_size;
            const u32 within = static_cast<u32>(offset - sector);
            const usz amount = length < source_sector_size - within
                                   ? length
                                   : source_sector_size - within;
            u8        block[4096]{};

            if (_source->read(block, source_sector_size, sector) != source_sector_size)
                return false;

            for (usz i = 0; i < amount; ++i) {
                output[i] = block[within + i];
            }

            output += amount;
            offset += amount;
            length -= amount;
        }

        return true;
    }

    bool mount::initialize() {
        if (!_source || !_source->mode().is_block_device())
            return false;

        if (!_reader.open(read_source, this) || !_reader.read_inode(2, _root))
            return false;

        if ((_reader.feature_incompat() & 0x0002) == 0)
            return false;

        return _root.type() == ext4::node_type::directory;
    }

    bool mount::child(const ext4::inode& parent, const char* name,
                           ext4::inode& node) const {
        if (parent.type() != ext4::node_type::directory)
            return false;

        lookup_context context{name};

        if (!_reader.read_directory(parent, lookup_entry, &context) || !context.found)
            return false;

        return _reader.read_inode(context.inode, node);
    }

    bool mount::lookup(const char* path, ext4::inode& node) const {
        if (path == nullptr || path[0] != '/')
            return false;

        node = _root;

        u32 at = 0;

        while (path[at] != '\0') {
            while (path[at] == '/') {
                ++at;
            }

            if (path[at] == '\0')
                break;

            char component[fs::path_max]{};
            u32  length = 0;

            while (path[at] != '\0' && path[at] != '/') {
                if (length + 1 >= fs::path_max)
                    return false;

                component[length++] = path[at++];
            }

            component[length] = '\0';

            if (string_equal(component, "."))
                continue;

            ext4::inode child_node;

            if (!child(node, component, child_node))
                return false;

            node = child_node;
        }

        return true;
    }

    fs::file_type mount::vfs_type(ext4::node_type type) {
        return convert_type(type);
    }

    fs::file_mode mount::vfs_mode(const ext4::inode& node) {
        const fs::file_type type = vfs_type(node.type());

        return fs::file_mode(type, fs::file_permissions(static_cast<u16>(node.mode & 07777)));
    }

    mm::shptr<fs::open_file> mount::open(const char* path, u32 flags) {
        if (path == nullptr || (flags & (ASTIA_AIO_O_ACCMODE | ASTIA_AIO_O_CREAT |
                                         ASTIA_AIO_O_EXCL | ASTIA_AIO_O_TRUNC)) != 0)
            return {};

        ext4::inode node;

        if (!lookup(path, node) || ((flags & ASTIA_AIO_O_DIRECTORY) != 0 &&
                                    node.type() != ext4::node_type::directory))
            return {};

        if (node.type() != ext4::node_type::regular &&
            node.type() != ext4::node_type::directory)
            return {};

        auto file = mm::share<ext4::file>(this, node);

        return file ? mm::share<fs::open_file>(file, flags) : mm::shptr<fs::open_file>{};
    }

    bool mount::read_file(const ext4::inode& node, void* buffer, u32 length,
                               u64 offset) {
        return _reader.read_file(node, offset, buffer, length);
    }

    long mount::read_directory(const char* path, u64 index, char* name, u32 capacity,
                            fs::file_type* type, u64* inode) {
        if (path == nullptr || name == nullptr || type == nullptr || capacity == 0)
            return -EINVAL;

        ext4::inode node;

        if (!lookup(path, node))
            return -ENOENT;

        if (node.type() != ext4::node_type::directory)
            return -ENOTDIR;

        directory_context context{index, 0, name, capacity, type, inode};

        if (!_reader.read_directory(node, enumerate_entry, &context))
            return -EIO;

        return context.result;
    }

    long mount::readlink(const char* path, char* buffer, u32 capacity) const {
        if (path == nullptr || buffer == nullptr || capacity == 0)
            return -EINVAL;

        ext4::inode node;

        if (!lookup(path, node))
            return -ENOENT;

        if (node.type() != ext4::node_type::symlink)
            return -EINVAL;

        if (node.size >= capacity)
            return -ENAMETOOLONG;

        if (node.size <= sizeof node.block) {
            for (u64 i = 0; i < node.size; ++i) {
                buffer[i] = static_cast<char>(node.block[i]);
            }
        }
        else if (!_reader.read_file(node, 0, buffer, static_cast<usz>(node.size))) {
            return -EIO;
        }

        buffer[node.size] = '\0';

        return static_cast<long>(node.size);
    }

    fs::file_type mount::node_type(const char* path) const {
        ext4::inode node;

        return lookup(path, node) ? vfs_type(node.type()) : fs::directory_unknown;
    }

    fs::file_mode mount::node_mode(const char* path) const {
        ext4::inode node;

        return lookup(path, node) ? vfs_mode(node) : fs::file_mode{};
    }

    u64 mount::node_inode(const char* path) const {
        ext4::inode node;

        return lookup(path, node) ? node.number : 0;
    }

    uid_t mount::node_uid(const char* path) const {
        ext4::inode node;

        return lookup(path, node) ? uid_t(node.uid) : uid_t{};
    }

    gid_t mount::node_gid(const char* path) const {
        ext4::inode node;

        return lookup(path, node) ? gid_t(node.gid) : gid_t{};
    }

    fs::mount* filesystem::mount(const char* source) const {
        if (source == nullptr || source[0] != '/')
            return nullptr;

        auto device = fs::open(source, ASTIA_AIO_O_RDONLY);
        if (!device)
            return nullptr;

        auto* instance = new ext4::mount(device);

        if (instance == nullptr || !instance->initialize()) {
            delete instance;

            return nullptr;
        }

        return instance;
    }

    filesystem& type() {
        static filesystem instance;

        return instance;
    }
} // namespace astia::ext4
