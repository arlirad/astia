#include "ext4_extent.hpp"

namespace astia::ext4 {
    namespace {
        constexpr u16 extent_root_entries = 4;
        constexpr u16 extent_max_depth    = 5;
        constexpr u16 extent_max_length   = 32768;

        struct geometry {
            u32  block_size = 0;
            u32  seed       = 0;
            u16  node_max   = 0;
            bool csum       = false;
        };

        struct split_result {
            bool split = false;
            u64  block = 0;
            u32  key   = 0;
            u32  first = 0;
        };

        void copy_bytes(u8* destination, const u8* source, usz length) {
            for (usz i = 0; i < length; ++i) {
                destination[i] = source[i];
            }
        }

        geometry make_geometry(const volume& v) {
            geometry g;

            g.block_size = v.block_size();
            g.seed       = v.checksum_seed();
            g.csum       = v.metadata_csum();

            const u32 tail = g.csum ? 4 : 0;

            g.node_max = static_cast<u16>((g.block_size - 12 - tail) / 12);

            return g;
        }

        u32 extent_length(u16 encoded) {
            return encoded > extent_max_length ? encoded - extent_max_length
                                               : encoded;
        }

        u64 extent_start(const u8* entry) {
            return static_cast<u64>(load16(entry + 6)) << 32 | load32(entry + 8);
        }

        u64 index_child(const u8* entry) {
            return static_cast<u64>(load16(entry + 8)) << 32 | load32(entry + 4);
        }

        void store_extent(u8* entry, u32 logical, u32 length, u64 physical) {
            store32(entry, logical);
            store16(entry + 4, static_cast<u16>(length));
            store16(entry + 6, static_cast<u16>(physical >> 32));
            store32(entry + 8, static_cast<u32>(physical));
        }

        void store_index(u8* entry, u32 key, u64 child) {
            store32(entry, key);
            store32(entry + 4, static_cast<u32>(child));
            store16(entry + 8, static_cast<u16>(child >> 32));
            store16(entry + 10, 0);
        }

        const u8* node_entry(const u8* block, u16 index) {
            return block + 12 + static_cast<u32>(index) * 12;
        }

        u8* node_entry(u8* block, u16 index) {
            return block + 12 + static_cast<u32>(index) * 12;
        }

        bool read_header(const geometry& g, const u8* block, bool root, u16 depth,
                         u16& entries, u16& maximum) {
            if (load16(block) != extent_magic)
                return false;

            entries = load16(block + 2);
            maximum = load16(block + 4);

            if (load16(block + 6) != depth || depth > extent_max_depth)
                return false;

            if (maximum == 0 || entries > maximum)
                return false;

            const u16 capacity = root ? extent_root_entries : g.node_max;

            return maximum <= capacity;
        }

        bool verify_node_checksum(const geometry& g, const u8* block, u32 inode,
                                  u32 generation) {
            if (!g.csum)
                return true;

            u8 number_bytes[4];
            u8 generation_bytes[4];

            store32(number_bytes, inode);
            store32(generation_bytes, generation);

            u32 value = crc32c(g.seed, number_bytes, sizeof number_bytes);

            value = crc32c(value, generation_bytes, sizeof generation_bytes);
            value = crc32c(value, block, g.block_size - 4);

            return value == load32(block + g.block_size - 4);
        }

        void store_node_checksum(const geometry& g, u8* block, u32 inode,
                                 u32 generation) {
            if (!g.csum)
                return;

            u8 number_bytes[4];
            u8 generation_bytes[4];

            store32(number_bytes, inode);
            store32(generation_bytes, generation);

            u32 value = crc32c(g.seed, number_bytes, sizeof number_bytes);

            value = crc32c(value, generation_bytes, sizeof generation_bytes);
            value = crc32c(value, block, g.block_size - 4);

            store32(block + g.block_size - 4, value);
        }

        bool validate_leaf(volume& v, const u8* block, u16 entries, u32 minimum) {
            u64 previous_end = 0;

            for (u16 i = 0; i < entries; ++i) {
                const u8* entry   = node_entry(block, i);
                const u32 first   = load32(entry);
                const u16 encoded = load16(entry + 4);
                const u32 length  = extent_length(encoded);

                if (length == 0)
                    return false;

                if (i == 0) {
                    if (first < minimum)
                        return false;
                }
                else if (static_cast<u64>(first) < previous_end)
                    return false;

                const u64 logical_end = static_cast<u64>(first) + length;

                if (logical_end > (u64{1} << 32))
                    return false;

                const u64 start        = extent_start(entry);
                const u64 physical_end = start + length;

                if (start == 0 || physical_end < start ||
                    physical_end > v.blocks_count())
                    return false;

                previous_end = logical_end;
            }

            return true;
        }

        bool locate_node(volume& v, const geometry& g, const inode_record& node,
                         const u8* block, bool root, u16 depth, u32 minimum,
                         u32 logical, u64& physical, bool& found) {
            u16 entries = 0;
            u16 maximum = 0;

            if (!read_header(g, block, root, depth, entries, maximum))
                return false;

            if (depth == 0) {
                if (!validate_leaf(v, block, entries, minimum))
                    return false;

                for (u16 i = 0; i < entries; ++i) {
                    const u8* entry   = node_entry(block, i);
                    const u32 first   = load32(entry);
                    const u16 encoded = load16(entry + 4);
                    const u32 length  = extent_length(encoded);

                    if (logical >= first && logical - first < length) {
                        physical = encoded > extent_max_length
                                       ? 0
                                       : extent_start(entry) + (logical - first);

                        if (physical != 0 && physical >= v.blocks_count())
                            return false;

                        found = true;

                        return true;
                    }
                }

                physical = 0;
                found    = false;

                return true;
            }

            const u8* selected = nullptr;
            u32       previous = 0;

            for (u16 i = 0; i < entries; ++i) {
                const u8* entry = node_entry(block, i);

                const u32 first = load32(entry);
                const u64 child = index_child(entry);

                if (i == 0) {
                    if (first != minimum)
                        return false;
                }
                else if (first <= previous)
                    return false;

                if (load16(entry + 10) != 0 || child == 0 ||
                    child >= v.blocks_count())
                    return false;

                previous = first;

                if (first <= logical)
                    selected = entry;
            }

            if (selected == nullptr)
                return false;

            const u64 child     = index_child(selected);
            const u32 child_key = load32(selected);

            u8 buffer[4096];

            if (!v.read_block(child, buffer))
                return false;

            if (!verify_node_checksum(g, buffer, node.number, node.generation()))
                return false;

            return locate_node(v, g, node, buffer, false,
                               static_cast<u16>(depth - 1), child_key, logical,
                               physical, found);
        }

        bool read_leaf(volume& v, inode_record& node, const geometry& g,
                       u32 logical, u8* scratch, u8*& leaf, bool& root,
                       u64& block_number) {
            u8*  block  = node.block_area();
            u16  depth  = load16(block + 6);
            u16  entries = 0;
            u16  maximum = 0;
            bool is_root = true;
            u32  minimum = 0;

            if (!read_header(g, block, true, depth, entries, maximum))
                return false;

            while (depth > 0) {
                const u8* selected = nullptr;
                u32       previous = 0;

                for (u16 i = 0; i < entries; ++i) {
                    const u8* entry = node_entry(block, i);

                    const u32 first = load32(entry);
                    const u64 child = index_child(entry);

                    if (i == 0) {
                        if (first != minimum)
                            return false;
                    }
                    else if (first <= previous)
                        return false;

                    if (load16(entry + 10) != 0 || child == 0 ||
                        child >= v.blocks_count())
                        return false;

                    previous = first;

                    if (first <= logical)
                        selected = entry;
                }

                if (selected == nullptr)
                    return false;

                const u64 child     = index_child(selected);
                const u32 child_key = load32(selected);

                if (!v.read_block(child, scratch))
                    return false;

                if (!verify_node_checksum(g, scratch, node.number,
                                          node.generation()))
                    return false;

                block        = scratch;
                block_number = child;
                is_root      = false;
                minimum      = child_key;
                --depth;

                if (!read_header(g, block, false, depth, entries, maximum))
                    return false;
            }

            if (!validate_leaf(v, block, entries, minimum))
                return false;

            leaf = block;
            root = is_root;

            return true;
        }

        bool insert_extent_entry(u8* block, u32 logical, u64 physical) {
            const u16 entries = load16(block + 2);
            const u16 maximum = load16(block + 4);

            if (entries >= maximum)
                return false;

            u16 position = 0;

            while (position < entries) {
                const u8* entry  = node_entry(block, position);
                const u32 first  = load32(entry);
                const u32 length = extent_length(load16(entry + 4));

                if (length == 0)
                    return false;

                if (logical < first)
                    break;

                if (logical - first < length)
                    return false;

                ++position;
            }

            for (u16 i = entries; i > position; --i) {
                copy_bytes(node_entry(block, i), node_entry(block, i - 1), 12);
            }

            store_extent(node_entry(block, position), logical, 1, physical);
            store16(block + 2, static_cast<u16>(entries + 1));

            return true;
        }

        bool insert_index_entry(u8* block, u32 key, u64 child) {
            const u16 entries = load16(block + 2);
            const u16 maximum = load16(block + 4);

            if (entries >= maximum)
                return false;

            u16 position = 0;

            while (position < entries) {
                const u32 existing = load32(node_entry(block, position));

                if (existing == key)
                    return false;

                if (existing > key)
                    break;

                ++position;
            }

            for (u16 i = entries; i > position; --i) {
                copy_bytes(node_entry(block, i), node_entry(block, i - 1), 12);
            }

            store_index(node_entry(block, position), key, child);
            store16(block + 2, static_cast<u16>(entries + 1));

            return true;
        }

        class editor {
            public:
            editor(volume& v, inode_record& node, space& alloc)
                : _v(v), _node(node), _alloc(alloc), _g(make_geometry(v)),
                  _inode(node.number), _generation(node.generation()) {}

            u32 new_count() const {
                return _count;
            }

            bool insert(u32 logical, u64 physical) {
                u8* root = _node.block_area();
                u8  backup[60];

                copy_bytes(backup, root, sizeof backup);
                _count = 0;

                if (insert_impl(logical, physical))
                    return true;

                for (u32 i = _count; i != 0; --i) {
                    (void) _alloc.free_block(_new_blocks[i - 1]);
                }

                _count = 0;
                copy_bytes(root, backup, sizeof backup);

                return false;
            }

            bool try_merge(u32 logical, u64 physical, bool& merged) {
                merged = false;

                u8  scratch[4096];
                u8* leaf        = nullptr;
                bool leaf_root  = false;
                u64 leaf_block  = 0;

                if (!read_leaf(_v, _node, _g, logical, scratch, leaf, leaf_root,
                               leaf_block))
                    return false;

                const u16 entries = load16(leaf + 2);
                const u16 maximum = load16(leaf + 4);

                if (entries > maximum)
                    return false;

                if (entries == 0)
                    return true;

                u16 position = 0;

                while (position < entries) {
                    if (load32(node_entry(leaf, position)) > logical)
                        break;

                    ++position;
                }

                if (position == 0)
                    return true;

                u8*       previous = node_entry(leaf, static_cast<u16>(position - 1));
                const u16 encoded  = load16(previous + 4);
                const u32 length   = extent_length(encoded);

                if (encoded > extent_max_length || length >= extent_max_length)
                    return true;

                if (static_cast<u64>(load32(previous)) + length != logical)
                    return true;

                if (extent_start(previous) + length != physical)
                    return true;

                store16(previous + 4, static_cast<u16>(length + 1));

                if (!leaf_root && !write_node(leaf_block, leaf))
                    return false;

                merged = true;

                return true;
            }

            private:
            bool insert_impl(u32 logical, u64 physical) {
                u8* root = _node.block_area();

                u16 entries = 0;
                u16 maximum = 0;
                u16 depth   = load16(root + 6);

                if (!read_header(_g, root, true, depth, entries, maximum))
                    return false;

                bool full = false;

                if (!leaf_full(logical, full))
                    return false;

                if (!full) {
                    u8  scratch[4096];
                    u8* leaf       = nullptr;
                    bool leaf_root = false;
                    u64 leaf_block = 0;

                    if (!read_leaf(_v, _node, _g, logical, scratch, leaf,
                                   leaf_root, leaf_block))
                        return false;

                    if (!insert_extent_entry(leaf, logical, physical))
                        return false;

                    if (!leaf_root && !write_node(leaf_block, leaf))
                        return false;

                    return true;
                }

                if (entries >= extent_root_entries && !grow_root())
                    return false;

                split_result result;

                if (!insert_at(root, true, 0, load16(root + 6), 0, logical,
                               physical, result))
                    return false;

                return !result.split;
            }

            bool leaf_full(u32 logical, bool& full) {
                u8  scratch[4096];
                u8* leaf       = nullptr;
                bool leaf_root = false;
                u64 leaf_block = 0;

                if (!read_leaf(_v, _node, _g, logical, scratch, leaf, leaf_root,
                               leaf_block))
                    return false;

                full = load16(leaf + 2) >= load16(leaf + 4);

                return true;
            }

            bool grow_root() {
                u8* root = _node.block_area();

                const u16 depth = load16(root + 6);

                if (depth >= extent_max_depth)
                    return false;

                u64 block = 0;

                if (!push_alloc(block))
                    return false;

                u8 copy[4096];

                for (u32 i = 0; i < _g.block_size; ++i) {
                    copy[i] = 0;
                }

                copy_bytes(copy, root, 60);
                store16(copy + 4, _g.node_max);
                store32(copy + 8, 0);

                if (!write_node(block, copy))
                    return false;

                for (u32 i = 12; i < 60; ++i) {
                    root[i] = 0;
                }

                store16(root + 2, 1);
                store16(root + 4, extent_root_entries);
                store16(root + 6, static_cast<u16>(depth + 1));
                store32(root + 8, 0);
                store_index(root + 12, 0, block);

                return true;
            }

            bool insert_at(u8* block, bool root, u64 block_number, u16 depth,
                           u32 minimum, u32 logical, u64 physical,
                           split_result& out) {
                u16 entries = 0;
                u16 maximum = 0;

                if (!read_header(_g, block, root, depth, entries, maximum))
                    return false;

                if (depth == 0) {
                    if (entries < maximum) {
                        if (!insert_extent_entry(block, logical, physical))
                            return false;

                        if (!root && !write_node(block_number, block))
                            return false;

                        out.split = false;
                        out.first = load32(node_entry(block, 0));

                        return true;
                    }

                    u64 new_block = 0;

                    if (!push_alloc(new_block))
                        return false;

                    u8 right[4096];

                    for (u32 i = 0; i < _g.block_size; ++i) {
                        right[i] = 0;
                    }

                    copy_bytes(right, block, 12);

                    const u16 half        = static_cast<u16>(entries / 2);
                    const u16 right_count = static_cast<u16>(entries - half);

                    store16(right + 2, right_count);
                    store16(right + 4, maximum);
                    store16(right + 6, 0);
                    store32(right + 8, 0);

                    for (u16 i = 0; i < right_count; ++i) {
                        copy_bytes(node_entry(right, i),
                                   node_entry(block, static_cast<u16>(half + i)), 12);
                    }

                    store16(block + 2, half);

                    if (logical < load32(node_entry(right, 0))) {
                        if (!insert_extent_entry(block, logical, physical))
                            return false;
                    }
                    else if (!insert_extent_entry(right, logical, physical))
                        return false;

                    if (!write_node(new_block, right))
                        return false;

                    if (!root && !write_node(block_number, block))
                        return false;

                    out.split = true;
                    out.block = new_block;
                    out.key   = load32(node_entry(right, 0));
                    out.first = load32(node_entry(block, 0));

                    return true;
                }

                const u8* selected      = nullptr;
                u32       previous      = 0;
                u16       selected_index = 0;

                for (u16 i = 0; i < entries; ++i) {
                    const u8* entry = node_entry(block, i);

                    const u32 first = load32(entry);
                    const u64 child = index_child(entry);

                    if (i == 0) {
                        if (first != minimum)
                            return false;
                    }
                    else if (first <= previous)
                        return false;

                    if (load16(entry + 10) != 0 || child == 0 ||
                        child >= _v.blocks_count())
                        return false;

                    previous = first;

                    if (first <= logical) {
                        selected       = entry;
                        selected_index = i;
                    }
                }

                if (selected == nullptr)
                    return false;

                const u64 child_block = index_child(selected);
                const u32 child_key   = load32(selected);

                u8 child_buffer[4096];

                if (!_v.read_block(child_block, child_buffer))
                    return false;

                if (!verify_node_checksum(_g, child_buffer, _node.number,
                                          _node.generation()))
                    return false;

                split_result sub;

                if (!insert_at(child_buffer, false, child_block,
                               static_cast<u16>(depth - 1), child_key, logical,
                               physical, sub))
                    return false;

                if (!sub.split) {
                    if (sub.first < child_key)
                        store32(node_entry(block, selected_index), sub.first);

                    if (!root && !write_node(block_number, block))
                        return false;

                    out.split = false;
                    out.first = load32(node_entry(block, 0));

                    return true;
                }

                if (entries < maximum) {
                    if (sub.first < child_key)
                        store32(node_entry(block, selected_index), sub.first);

                    if (!insert_index_entry(block, sub.key, sub.block))
                        return false;

                    if (!root && !write_node(block_number, block))
                        return false;

                    out.split = false;
                    out.first = load32(node_entry(block, 0));

                    return true;
                }

                u64 new_block = 0;

                if (!push_alloc(new_block))
                    return false;

                u8 right[4096];

                for (u32 i = 0; i < _g.block_size; ++i) {
                    right[i] = 0;
                }

                copy_bytes(right, block, 12);

                const u16 half        = static_cast<u16>(entries / 2);
                const u16 right_count = static_cast<u16>(entries - half);

                store16(right + 2, right_count);
                store16(right + 4, maximum);
                store16(right + 6, depth);
                store32(right + 8, 0);

                for (u16 i = 0; i < right_count; ++i) {
                    copy_bytes(node_entry(right, i),
                               node_entry(block, static_cast<u16>(half + i)), 12);
                }

                store16(block + 2, half);

                if (sub.first < child_key && selected_index < half)
                    store32(node_entry(block, selected_index), sub.first);

                if (sub.key < load32(node_entry(right, 0))) {
                    if (!insert_index_entry(block, sub.key, sub.block))
                        return false;
                }
                else if (!insert_index_entry(right, sub.key, sub.block))
                    return false;

                if (!write_node(new_block, right))
                    return false;

                if (!root && !write_node(block_number, block))
                    return false;

                out.split = true;
                out.block = new_block;
                out.key   = load32(node_entry(right, 0));
                out.first = load32(node_entry(block, 0));

                return true;
            }

            bool push_alloc(u64& block) {
                if (_count >= sizeof _new_blocks / sizeof _new_blocks[0])
                    return false;

                u64 value = 0;

                if (!_alloc.alloc_block(value))
                    return false;

                _new_blocks[_count++] = value;
                block                 = value;

                return true;
            }

            bool write_node(u64 block_number, u8* block) {
                store_node_checksum(_g, block, _inode, _generation);

                return _v.write_block(block_number, block);
            }

            volume&       _v;
            inode_record& _node;
            space&        _alloc;
            geometry      _g;
            u32           _inode;
            u32           _generation;
            u64           _new_blocks[8]{};
            u32           _count = 0;
        };

        bool free_range(space& alloc, u64 start, u32 length, u32& freed) {
            for (u32 i = 0; i < length; ++i) {
                if (!alloc.free_block(start + i))
                    return false;

                ++freed;
            }

            return true;
        }

        bool release_extent(space& alloc, const u8* entry, u32 from,
                            u32& freed) {
            const u16 encoded = load16(entry + 4);
            const u32 length  = extent_length(encoded);
            const u64 start   = extent_start(entry);

            // Unwritten (preallocated) extents own their physical blocks too;
            // only their read semantics differ, so free them like any other.
            if (start == 0 || from >= length)
                return true;

            return free_range(alloc, start + from, length - from, freed);
        }

        bool prune_leaf(space& alloc, u8* block, u32 keep, u32& freed,
                        bool& changed) {
            u16 entries = load16(block + 2);

            changed = false;

            u16 position = 0;

            while (position < entries) {
                u8*       entry   = node_entry(block, position);
                const u32 first   = load32(entry);
                const u16 encoded = load16(entry + 4);
                const u32 length  = extent_length(encoded);

                if (first >= keep) {
                    if (!release_extent(alloc, entry, 0, freed))
                        return false;

                    for (u16 i = position; i + 1 < entries; ++i) {
                        copy_bytes(node_entry(block, i),
                                   node_entry(block, static_cast<u16>(i + 1)), 12);
                    }

                    --entries;
                    store16(block + 2, entries);
                    changed = true;

                    continue;
                }

                if (static_cast<u64>(first) + length > keep) {
                    const u32 keep_length = keep - first;

                    if (!release_extent(alloc, entry, keep_length, freed))
                        return false;

                    store16(entry + 4, static_cast<u16>(keep_length));
                    changed = true;
                }

                ++position;
            }

            return true;
        }

        bool prune_node(volume& v, space& alloc, const geometry& g,
                        inode_record& node, u8* block, bool root,
                        u64 block_number, u16 depth, u32 keep, u32& freed) {
            u16 entries = 0;
            u16 maximum = 0;

            if (!read_header(g, block, root, depth, entries, maximum))
                return false;

            if (depth == 0) {
                bool changed = false;

                if (!prune_leaf(alloc, block, keep, freed, changed))
                    return false;

                if (changed && !root) {
                    store_node_checksum(g, block, node.number,
                                        node.generation());

                    if (!v.write_block(block_number, block))
                        return false;
                }

                return true;
            }

            bool changed = false;
            u16  position = 0;

            while (position < entries) {
                u8* entry = node_entry(block, position);

                const u64 child = index_child(entry);

                if (child == 0 || child >= v.blocks_count())
                    return false;

                u8 child_block[4096];

                if (!v.read_block(child, child_block))
                    return false;

                if (!verify_node_checksum(g, child_block, node.number,
                                          node.generation()))
                    return false;

                if (!prune_node(v, alloc, g, node, child_block, false, child,
                                static_cast<u16>(depth - 1), keep, freed))
                    return false;

                if (load16(child_block + 2) == 0) {
                    if (!alloc.free_block(child))
                        return false;

                    ++freed;

                    for (u16 i = position; i + 1 < entries; ++i) {
                        copy_bytes(node_entry(block, i),
                                   node_entry(block, static_cast<u16>(i + 1)), 12);
                    }

                    --entries;
                    store16(block + 2, entries);
                    changed = true;

                    continue;
                }

                const u32 child_key = load32(node_entry(child_block, 0));

                if (child_key != load32(entry)) {
                    store32(entry, child_key);
                    changed = true;
                }

                ++position;
            }

            if (root) {
                if (entries == 0) {
                    for (u32 i = 0; i < 60; ++i) {
                        block[i] = 0;
                    }

                    store16(block, extent_magic);
                    store16(block + 2, 0);
                    store16(block + 4, extent_root_entries);
                    store16(block + 6, 0);
                    store32(block + 8, 0);
                }

                return true;
            }

            if (changed) {
                store_node_checksum(g, block, node.number, node.generation());

                if (!v.write_block(block_number, block))
                    return false;
            }

            return true;
        }
    } // namespace

    bool extent::map(volume& v, const inode_record& node, u32 logical,
                     u64& physical) {
        if ((node.flags() & inode_inline_data) != 0)
            return false;

        if ((node.flags() & inode_extents) == 0)
            return false;

        const geometry g    = make_geometry(v);
        const u8*      root = node.block_area();
        const u16      depth = load16(root + 6);

        bool found = false;

        physical = 0;

        return locate_node(v, g, node, root, true, depth, 0, logical, physical,
                           found);
    }

    u64 extent::next_physical(const inode_record& node) {
        if ((node.flags() & inode_inline_data) != 0 ||
            (node.flags() & inode_extents) == 0)
            return 0;

        const u8* root  = node.block_area();
        const u16 depth = load16(root + 6);

        if (load16(root) != extent_magic || depth != 0)
            return 0;

        const u16 entries = load16(root + 2);
        const u16 maximum = load16(root + 4);

        if (entries == 0 || entries > maximum || maximum > extent_root_entries)
            return 0;

        const u8* last = node_entry(root, static_cast<u16>(entries - 1));

        return extent_start(last) + extent_length(load16(last + 4));
    }

    bool extent::map_or_alloc(volume& v, space& alloc, inode_record& node,
                              u32 logical, u64& physical) {
        if (!v.writable())
            return false;

        if ((node.flags() & inode_inline_data) != 0)
            return false;

        if ((node.flags() & inode_extents) == 0)
            return false;

        const geometry g     = make_geometry(v);
        const u8*      root  = node.block_area();
        const u16      depth = load16(root + 6);

        bool found    = false;
        u64  existing = 0;

        if (!locate_node(v, g, node, root, true, depth, 0, logical, existing,
                         found))
            return false;

        if (found) {
            if (existing == 0)
                return false;

            physical = existing;

            return true;
        }

        u64 block = 0;

        if (!alloc.alloc_block(block))
            return false;

        editor edit(v, node, alloc);
        bool   merged = false;

        if (!edit.try_merge(logical, block, merged)) {
            (void) alloc.free_block(block);

            return false;
        }

        if (!merged && !edit.insert(logical, block)) {
            (void) alloc.free_block(block);

            return false;
        }

        const u64 sectors = static_cast<u64>(v.block_size() / 512);

        node.set_blocks(node.blocks() + sectors * (1 + edit.new_count()));

        physical = block;

        return true;
    }

    bool extent::truncate(volume& v, space& alloc, inode_record& node,
                          u64 new_size) {
        if (!v.writable())
            return false;

        if ((node.flags() & inode_inline_data) != 0 ||
            (node.flags() & inode_extents) == 0)
            return false;

        const u64 block_size = v.block_size();

        if (block_size == 0)
            return false;

        if (new_size >= node.size()) {
            node.set_size(new_size);

            return true;
        }

        const u64 keep_wide =
            new_size / block_size + (new_size % block_size != 0 ? 1 : 0);

        if (keep_wide > ~u32{0})
            return false;

        const geometry g    = make_geometry(v);
        u8*            root = node.block_area();
        const u16      depth = load16(root + 6);

        if (load16(root) != extent_magic || depth > extent_max_depth)
            return false;

        u32 freed = 0;

        if (!prune_node(v, alloc, g, node, root, true, 0, depth,
                        static_cast<u32>(keep_wide), freed))
            return false;

        const u64 sectors   = block_size / 512;
        const u64 reclaimed = static_cast<u64>(freed) * sectors;

        node.set_blocks(node.blocks() > reclaimed ? node.blocks() - reclaimed
                                                  : 0);
        node.set_size(new_size);

        if (new_size == 0)
            node.set_blocks(0);

        return true;
    }

    bool extent::free_all(volume& v, space& alloc, inode_record& node) {
        return truncate(v, alloc, node, 0);
    }

    u64 extent::max_blocks(const volume& v) {
        const geometry g     = make_geometry(v);
        const u64      bound = u64{1} << 32;

        if (g.node_max == 0)
            return 0;

        u64 extents = extent_root_entries;

        for (u16 depth = 0; depth < extent_max_depth; ++depth) {
            if (extents > bound / g.node_max)
                return bound;

            extents *= g.node_max;
        }

        if (extents > bound / extent_max_length)
            return bound;

        extents *= extent_max_length;

        return extents > bound ? bound : extents;
    }
} // namespace astia::ext4
