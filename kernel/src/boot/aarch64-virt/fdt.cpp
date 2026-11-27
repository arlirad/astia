#include <astia/boot/aarch64/fdt.hpp>
#include <astia/core/endian.hpp>

namespace astia::boot::aarch64::fdt {
    namespace {
        constexpr u32 header_size      = 40;
        constexpr u32 max_depth        = 64;
        constexpr u32 max_cell_count   = 4;
        constexpr u32 max_decode_cells = 2;

        constexpr usz max_aliases                = 16;
        constexpr usz alias_name_size            = 32;
        constexpr usz max_memory_ranges_per_node = 4;
        constexpr usz max_register_pairs         = 2;
        constexpr usz max_console_nodes          = 8;
        constexpr usz console_path_size          = virt::console_path_capacity;
        constexpr u32 gic_interrupt_cells        = 3;
        constexpr u32 gic_redistributor_regions  = 1;

        constexpr u32 token_begin_node = 0x00000001;
        constexpr u32 token_end_node   = 0x00000002;
        constexpr u32 token_prop       = 0x00000003;
        constexpr u32 token_nop        = 0x00000004;
        constexpr u32 token_end        = 0x00000009;

        constexpr u32 min_version = 17;
        constexpr u32 min_compat  = 16;

        [[nodiscard]] bool add_checked(u64 left, u64 right, u64& result) {
            if (left > ~u64{0} - right)
                return false;

            result = left + right;

            return true;
        }

        struct address_range {
            u64 base = 0;
            u64 size = 0;
        };

        [[nodiscard]] bool ranges_overlap(const address_range& first,
                                          const address_range& second) {
            u64 first_end  = 0;
            u64 second_end = 0;

            if (!add_checked(first.base, first.size, first_end) ||
                !add_checked(second.base, second.size, second_end))
                return true;

            return first.base < second_end && second.base < first_end;
        }

        [[nodiscard]] bool align_up4(u32 value, u32& result) {
            if (value > 0xfffffffcU)
                return false;

            result = (value + 3U) & ~3U;

            return true;
        }

        [[nodiscard]] bool range_fits(u32 offset, u32 size, u32 total) {
            return offset <= total && size <= total - offset;
        }

        [[nodiscard]] bool equal_bytes(const u8* bytes, u32 length, const char* text) {
            u32 index = 0;

            while (index < length && text[index] != '\0') {
                if (bytes[index] != static_cast<u8>(text[index]))
                    return false;

                ++index;
            }

            return index == length && text[index] == '\0';
        }

        [[nodiscard]] bool equal_text(const char* first, const char* second) {
            while (*first != '\0' && *first == *second) {
                ++first;
                ++second;
            }

            return *first == *second;
        }

        [[nodiscard]] bool string_value_equals(const u8* value, u32 length,
                                               const char* text) {
            u32 text_length = 0;

            while (text[text_length] != '\0') {
                ++text_length;
            }

            return length == text_length + 1 && value[text_length] == '\0' &&
                   equal_bytes(value, text_length, text);
        }

        [[nodiscard]] bool valid_name_character(u8 value) {
            return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'z') ||
                   (value >= 'A' && value <= 'Z') || value == ',' || value == '.' ||
                   value == '_' || value == '+' || value == '-';
        }

        [[nodiscard]] bool valid_node_name(const u8* name, u32 length) {
            if (length == 0)
                return false;

            bool saw_unit_address = false;

            for (u32 index = 0; index < length; ++index) {
                if (name[index] == '@') {
                    if (saw_unit_address || index == 0 || index + 1 == length)
                        return false;

                    saw_unit_address = true;

                    continue;
                }

                if (!valid_name_character(name[index]))
                    return false;
            }

            return true;
        }

        [[nodiscard]] bool read_string(const u8* bytes, u32 length, char* destination,
                                       usz capacity) {
            u32 index = 0;

            while (index < length && bytes[index] != '\0') {
                if (index + 1 >= capacity)
                    return false;

                destination[index] = static_cast<char>(bytes[index]);

                ++index;
            }

            if (index == length)
                return false;

            destination[index] = '\0';

            return true;
        }

        [[nodiscard]] bool string_list_contains(const u8* value, u32 length,
                                                const char* expected) {
            u32 index = 0;

            while (index < length) {
                const u32 start = index;

                while (index < length && value[index] != '\0') {
                    ++index;
                }

                if (index == length)
                    return false;

                if (equal_bytes(value + start, index - start, expected))
                    return true;

                ++index;
            }

            return false;
        }

        [[nodiscard]] bool decode_cells(const u8* value, u32 count, u64& result) {
            if (count == 0 || count > max_decode_cells)
                return false;

            result = 0;

            for (u32 index = 0; index < count; ++index) {
                result = (result << 32) |
                         static_cast<u64>(core::be<u32>::load(value + index * 4));
            }

            return true;
        }

        struct node_frame {
            u32           path_length             = 0;
            u32           address_cells           = 2;
            u32           size_cells              = 1;
            u64           register_base           = 0;
            u64           register_size           = 0;
            bool          has_register            = false;
            bool          is_pl011                = false;
            bool          is_gic                  = false;
            bool          is_legacy_gic           = false;
            bool          is_interrupt_controller = false;
            bool          is_memory               = false;
            bool          device_type_memory      = false;
            u32           interrupt_cells         = 0;
            u32           redistributor_regions   = gic_redistributor_regions;
            address_range memory_ranges[max_memory_ranges_per_node]{};
            u32           memory_range_count = 0;
            address_range registers[max_register_pairs]{};
            u32           register_count = 0;
            u32           register_total = 0;
        };

        struct alias_entry {
            char name[alias_name_size]{};
            char target[console_path_size]{};
        };

        struct console_candidate {
            char path[console_path_size]{};
            u64  base = 0;
            u32  size = 0;
        };

        class parser {
            public:
            parser(const u8* blob, u32 window_size, console_policy console,
                   virt::boot_info& info)
                : blob_(blob), window_(window_size), console_(console), info_(info) {}

            [[nodiscard]] error run() {
                error status = parse_header();

                if (status != error::none)
                    return status;

                status = parse_reservations();

                if (status != error::none)
                    return status;

                status = parse_structure();

                if (status != error::none)
                    return status;

                if (!has_memory_)
                    return error::no_memory;

                resolve_console();

                info_.dtb      = blob_;
                info_.dtb_size = header_.totalsize;

                return error::none;
            }

            private:
            struct header {
                u32 magic          = 0;
                u32 totalsize      = 0;
                u32 off_struct     = 0;
                u32 off_strings    = 0;
                u32 off_rsvmap     = 0;
                u32 version        = 0;
                u32 last_compat    = 0;
                u32 size_strings   = 0;
                u32 size_structure = 0;
            };

            [[nodiscard]] error parse_header() {
                if (window_ < header_size)
                    return error::too_small;

                header_.magic          = core::be<u32>::load(blob_ + 0);
                header_.totalsize      = core::be<u32>::load(blob_ + 4);
                header_.off_struct     = core::be<u32>::load(blob_ + 8);
                header_.off_strings    = core::be<u32>::load(blob_ + 12);
                header_.off_rsvmap     = core::be<u32>::load(blob_ + 16);
                header_.version        = core::be<u32>::load(blob_ + 20);
                header_.last_compat    = core::be<u32>::load(blob_ + 24);
                header_.size_strings   = core::be<u32>::load(blob_ + 32);
                header_.size_structure = core::be<u32>::load(blob_ + 36);

                if (header_.magic != 0xd00dfeedU)
                    return error::bad_magic;

                if (header_.totalsize < header_size)
                    return error::too_small;

                if (header_.totalsize > window_)
                    return error::bad_bounds;

                if (header_.version < min_version || header_.last_compat < min_compat ||
                    header_.last_compat > header_.version)
                    return error::bad_version;

                if (header_.size_structure < 4)
                    return error::bad_structure;

                if ((header_.off_struct & 3U) != 0 || (header_.off_rsvmap & 7U) != 0)
                    return error::bad_alignment;

                if (!range_fits(header_.off_struct, header_.size_structure,
                                header_.totalsize) ||
                    !range_fits(header_.off_strings, header_.size_strings,
                                header_.totalsize) ||
                    !range_fits(header_.off_rsvmap, 0, header_.totalsize))
                    return error::bad_bounds;

                const u32 structure_end = header_.off_struct + header_.size_structure;
                const u32 strings_end   = header_.off_strings + header_.size_strings;

                if (header_.off_rsvmap < header_size ||
                    header_.off_struct < header_size || header_.off_strings < header_size)
                    return error::bad_bounds;

                if (!(structure_end <= header_.off_strings ||
                      strings_end <= header_.off_struct))
                    return error::bad_bounds;

                const auto starts_inside = [](u32 start, u32 end, u32 point) {
                    return start <= point && point < end;
                };

                if (starts_inside(header_.off_struct, structure_end,
                                  header_.off_rsvmap) ||
                    starts_inside(header_.off_strings, strings_end, header_.off_rsvmap))
                    return error::bad_bounds;

                reservation_limit_ = header_.totalsize;

                if (header_.off_struct > header_.off_rsvmap &&
                    header_.off_struct < reservation_limit_)
                    reservation_limit_ = header_.off_struct;

                if (header_.off_strings > header_.off_rsvmap &&
                    header_.off_strings < reservation_limit_)
                    reservation_limit_ = header_.off_strings;

                return error::none;
            }

            [[nodiscard]] error parse_reservations() {
                u32 cursor = header_.off_rsvmap;

                while (true) {
                    if (!range_fits(cursor, 16, reservation_limit_))
                        return error::bad_bounds;

                    const u64 address = core::be<u64>::load(blob_ + cursor);
                    const u64 size    = core::be<u64>::load(blob_ + cursor + 8);

                    cursor += 16;

                    if (address == 0 && size == 0)
                        return error::none;

                    u64 end = 0;

                    if (!add_checked(address, size, end))
                        return error::bad_cells;

                    if (info_.reserved.count() >= mm::memory_map::max_regions)
                        return error::too_many_regions;

                    info_.reserved.add(address, size, mm::memory_type::reserved);
                }
            }

            [[nodiscard]] error parse_structure() {
                u32       cursor       = header_.off_struct;
                const u32 end          = header_.off_struct + header_.size_structure;
                u32       token_budget = header_.size_structure / 4 + 1;

                while (cursor < end) {
                    if (token_budget == 0)
                        return error::bad_structure;

                    --token_budget;

                    if (!range_fits(cursor, 4, end))
                        return error::bad_structure;

                    const u32 token = core::be<u32>::load(blob_ + cursor);

                    cursor += 4;

                    if (token == token_end) {
                        if (depth_ != 0 || !root_seen_ || cursor != end)
                            return error::bad_structure;

                        return error::none;
                    }

                    if (token == token_nop)
                        continue;

                    if (token == token_begin_node) {
                        const error status = begin_node(cursor, end);

                        if (status != error::none)
                            return status;

                        continue;
                    }

                    if (token == token_end_node) {
                        const error status = end_node();

                        if (status != error::none)
                            return status;

                        continue;
                    }

                    if (token == token_prop) {
                        const error status = property(cursor, end);

                        if (status != error::none)
                            return status;

                        continue;
                    }

                    return error::bad_token;
                }

                return error::bad_structure;
            }

            [[nodiscard]] error begin_node(u32& cursor, u32 end) {
                if (depth_ >= max_depth)
                    return error::depth_overflow;

                if (depth_ == 0 && root_seen_)
                    return error::bad_structure;

                u32 name_end = cursor;

                while (name_end < end && blob_[name_end] != '\0') {
                    ++name_end;
                }

                if (name_end == end)
                    return error::bad_name;

                const u32 name_length = name_end - cursor;
                u32       padded      = 0;

                if (!align_up4(name_end + 1, padded) || padded > end)
                    return error::bad_structure;

                node_frame& frame = frames_[depth_];

                frame.path_length             = path_length_;
                frame.address_cells           = 2;
                frame.size_cells              = 1;
                frame.register_base           = 0;
                frame.register_size           = 0;
                frame.has_register            = false;
                frame.is_pl011                = false;
                frame.is_gic                  = false;
                frame.is_legacy_gic           = false;
                frame.is_interrupt_controller = false;
                frame.device_type_memory      = false;
                frame.interrupt_cells         = 0;
                frame.redistributor_regions   = gic_redistributor_regions;
                frame.memory_range_count      = 0;
                frame.register_count          = 0;
                frame.register_total          = 0;

                if (depth_ == 0) {
                    if (name_length != 0)
                        return error::bad_name;

                    root_seen_   = true;
                    path_[0]     = '/';
                    path_[1]     = '\0';
                    path_length_ = 1;
                }
                else {
                    if (!valid_node_name(blob_ + cursor, name_length))
                        return error::bad_name;

                    const bool parent_is_root = path_length_ == 1 && path_[0] == '/';

                    if (path_length_ + (parent_is_root ? 0 : 1) + name_length + 1 >
                        console_path_size)
                        return error::bad_name;

                    if (!parent_is_root)
                        path_[path_length_++] = '/';

                    for (u32 index = 0; index < name_length; ++index) {
                        path_[path_length_++] = static_cast<char>(blob_[cursor + index]);
                    }

                    path_[path_length_] = '\0';
                }

                cursor          = padded;
                frame.is_memory = depth_ != 0 && path_is_memory();

                ++depth_;

                return error::none;
            }

            [[nodiscard]] error parse_gic_node(const node_frame& frame) {
                if (!frame.is_interrupt_controller ||
                    frame.interrupt_cells != gic_interrupt_cells ||
                    frame.redistributor_regions != gic_redistributor_regions ||
                    frame.register_total != max_register_pairs ||
                    frame.register_count != max_register_pairs || info_.gic.present)
                    return error::bad_gic;

                for (u32 index = 0; index < frame.register_count; ++index) {
                    const address_range& range = frame.registers[index];

                    if (range.size == 0 || (range.base & 0xfffULL) != 0 ||
                        (range.size & 0xfffULL) != 0)
                        return error::bad_gic;
                }

                if (ranges_overlap(frame.registers[0], frame.registers[1]))
                    return error::bad_gic;

                info_.gic.distributor_base      = frame.registers[0].base;
                info_.gic.distributor_size      = frame.registers[0].size;
                info_.gic.redistributor_base    = frame.registers[1].base;
                info_.gic.redistributor_size    = frame.registers[1].size;
                info_.gic.redistributor_regions = frame.redistributor_regions;
                info_.gic.present               = true;

                return error::none;
            }

            [[nodiscard]] error end_node() {
                if (depth_ == 0)
                    return error::bad_structure;

                node_frame& frame = frames_[depth_ - 1];

                if (frame.is_legacy_gic && frame.is_interrupt_controller)
                    return error::bad_gic;

                if (frame.is_gic) {
                    const error status = parse_gic_node(frame);

                    if (status != error::none)
                        return status;
                }

                if (path_is_memory() && frame.device_type_memory) {
                    for (u32 index = 0; index < frame.memory_range_count; ++index) {
                        if (info_.memory.count() >= mm::memory_map::max_regions)
                            return error::too_many_regions;

                        info_.memory.add(frame.memory_ranges[index].base,
                                         frame.memory_ranges[index].size,
                                         mm::memory_type::usable);

                        has_memory_ = true;
                    }
                }

                if (frame.is_pl011 && frame.has_register) {
                    if (frame.register_size > 0xffffffffULL ||
                        console_count_ >= max_console_nodes)
                        return error::too_many_regions;

                    console_candidate& candidate = consoles_[console_count_++];

                    for (u32 index = 0; index <= path_length_; ++index) {
                        candidate.path[index] = path_[index];
                    }

                    candidate.base = frame.register_base;
                    candidate.size = static_cast<u32>(frame.register_size);
                }

                path_length_ = frame.path_length;
                if (path_length_ < console_path_size)
                    path_[path_length_] = '\0';

                --depth_;

                return error::none;
            }

            [[nodiscard]] error property(u32& cursor, u32 end) {
                if (depth_ == 0)
                    return error::bad_structure;

                if (!range_fits(cursor, 8, end))
                    return error::bad_bounds;

                const u32 length  = core::be<u32>::load(blob_ + cursor);
                const u32 nameoff = core::be<u32>::load(blob_ + cursor + 4);
                const u32 value   = cursor + 8;

                if (!range_fits(value, length, end))
                    return error::bad_bounds;

                u32 next = 0;

                if (!align_up4(value + length, next) || next > end)
                    return error::bad_structure;

                const char* name = nullptr;

                u32 name_len = 0;

                if (!property_name(nameoff, name, name_len))
                    return error::bad_string;

                const error status =
                    parse_property(name, name_len, blob_ + value, length);

                if (status != error::none)
                    return status;

                cursor = next;

                return error::none;
            }

            [[nodiscard]] bool property_name(u32 offset, const char*& name,
                                             u32& length) const {
                if (offset >= header_.size_strings)
                    return false;

                const u8* strings = blob_ + header_.off_strings + offset;

                const u32 limit = header_.size_strings - offset;
                u32       index = 0;

                while (index < limit && strings[index] != '\0') {
                    ++index;
                }

                if (index == limit || index == 0)
                    return false;

                name   = reinterpret_cast<const char*>(strings);
                length = index;

                return true;
            }

            [[nodiscard]] error parse_property(const char* name, u32 name_length,
                                               const u8* value, u32 length) {
                node_frame& frame = frames_[depth_ - 1];

                if (equal_bytes(reinterpret_cast<const u8*>(name), name_length,
                                "#address-cells"))
                    return parse_cells_property(value, length, frame.address_cells);

                if (equal_bytes(reinterpret_cast<const u8*>(name), name_length,
                                "#size-cells"))
                    return parse_cells_property(value, length, frame.size_cells);

                if (path_is("/chosen") &&
                    (equal_bytes(reinterpret_cast<const u8*>(name), name_length,
                                 "stdout-path") ||
                     equal_bytes(reinterpret_cast<const u8*>(name), name_length,
                                 "linux,stdout-path"))) {
                    if (!read_string(value, length, stdout_path_, console_path_size))
                        return error::bad_string;

                    has_stdout_ = true;

                    return error::none;
                }

                if (path_is("/aliases")) {
                    add_alias(name, name_length, value, length);

                    return error::none;
                }

                if (equal_bytes(reinterpret_cast<const u8*>(name), name_length,
                                "device_type")) {
                    frame.device_type_memory =
                        string_value_equals(value, length, "memory");

                    return error::none;
                }

                if (equal_bytes(reinterpret_cast<const u8*>(name), name_length,
                                "compatible")) {
                    frame.is_pl011 = string_list_contains(value, length, "arm,pl011");
                    frame.is_gic   = string_list_contains(value, length, "arm,gic-v3");
                    frame.is_legacy_gic =
                        string_list_contains(value, length, "arm,cortex-a15-gic") ||
                        string_list_contains(value, length, "arm,gic-400") ||
                        string_list_contains(value, length, "arm,gic-v2");

                    return error::none;
                }

                if (equal_bytes(reinterpret_cast<const u8*>(name), name_length,
                                "interrupt-controller")) {
                    if (length != 0)
                        return error::bad_cells;

                    frame.is_interrupt_controller = true;

                    return error::none;
                }

                if (equal_bytes(reinterpret_cast<const u8*>(name), name_length,
                                "#interrupt-cells"))
                    return parse_cells_property(value, length, frame.interrupt_cells);

                if (equal_bytes(reinterpret_cast<const u8*>(name), name_length,
                                "#redistributor-regions"))
                    return parse_cells_property(value, length,
                                                frame.redistributor_regions);

                if (equal_bytes(reinterpret_cast<const u8*>(name), name_length, "reg"))
                    return parse_register(value, length, frame);

                return error::none;
            }

            [[nodiscard]] error parse_cells_property(const u8* value, u32 length,
                                                     u32& destination) const {
                if (length != 4)
                    return error::bad_cells;

                const u32 cells = core::be<u32>::load(value);
                if (cells > max_cell_count)
                    return error::bad_cells;

                destination = cells;

                return error::none;
            }

            void add_alias(const char* name, u32 length, const u8* value,
                           u32 value_length) {
                if (alias_count_ >= max_aliases || length == 0 ||
                    length + 1 > alias_name_size)
                    return;

                alias_entry& alias = aliases_[alias_count_];

                for (u32 index = 0; index < length; ++index) {
                    alias.name[index] = name[index];
                }

                alias.name[length] = '\0';

                if (!read_string(value, value_length, alias.target, console_path_size))
                    return;

                ++alias_count_;
            }

            [[nodiscard]] error parse_register(const u8* value, u32 length,
                                               node_frame& frame) {
                if (depth_ < 2)
                    return error::none;

                const bool memory_node   = frame.is_memory;
                const bool reserved_node = path_is_reserved_child();

                const node_frame& parent = frames_[depth_ - 2];

                const u32 address_cells = parent.address_cells;
                const u32 size_cells    = parent.size_cells;

                if (address_cells == 0 || size_cells == 0 ||
                    address_cells > max_decode_cells || size_cells > max_decode_cells)
                    return memory_node || reserved_node ? error::bad_cells : error::none;

                const u32 pair_cells = address_cells + size_cells;
                const u32 pair_bytes = pair_cells * 4;

                if (length == 0 || (length % pair_bytes) != 0)
                    return memory_node || reserved_node ? error::bad_cells : error::none;

                const u32 pairs = length / pair_bytes;

                for (u32 index = 0; index < pairs; ++index) {
                    const u8* pair = value + index * pair_bytes;

                    u64 base = 0;
                    u64 size = 0;

                    if (!decode_cells(pair, address_cells, base) ||
                        !decode_cells(pair + address_cells * 4, size_cells, size))
                        return error::bad_cells;

                    u64 end = 0;

                    if (!add_checked(base, size, end))
                        return error::bad_cells;

                    ++frame.register_total;

                    if (frame.register_count < max_register_pairs)
                        frame.registers[frame.register_count++] = {base, size};

                    if (memory_node && size != 0) {
                        if (frame.memory_range_count >= max_memory_ranges_per_node)
                            return error::too_many_regions;

                        frame.memory_ranges[frame.memory_range_count++] = {base, size};
                    }

                    if (reserved_node && size != 0) {
                        if (info_.reserved.count() >= mm::memory_map::max_regions)
                            return error::too_many_regions;

                        info_.reserved.add(base, size, mm::memory_type::reserved);
                    }

                    if (index == 0 && !memory_node && !reserved_node) {
                        frame.register_base = base;
                        frame.register_size = size;
                        frame.has_register  = true;
                    }
                }

                return error::none;
            }

            [[nodiscard]] bool path_is(const char* text) const {
                return equal_bytes(reinterpret_cast<const u8*>(path_), path_length_,
                                   text);
            }

            [[nodiscard]] bool path_is_memory() const {
                constexpr const char* prefix = "/memory";

                constexpr u32 size = 7;

                if (path_length_ < size ||
                    !equal_bytes(reinterpret_cast<const u8*>(path_), size, prefix))
                    return false;

                return path_length_ == size || path_[size] == '@';
            }

            [[nodiscard]] bool path_is_reserved_child() const {
                constexpr const char* prefix = "/reserved-memory/";

                constexpr u32 size = 17;

                return path_length_ > size &&
                       equal_bytes(reinterpret_cast<const u8*>(path_), size, prefix);
            }

            void resolve_console() {
                info_.console.base   = 0;
                info_.console.size   = 0;
                info_.console.source = virt::console_source::platform_default;

                for (usz index = 0; index < virt::console_path_capacity; ++index) {
                    info_.console.path[index] = '\0';
                }

                if (!has_stdout_)
                    return;

                char target[console_path_size]{};
                u32  target_length = 0;

                for (u32 index = 0; index < console_path_size - 1; ++index) {
                    const char value = stdout_path_[index];

                    if (value == '\0' || value == ':')
                        break;

                    target[target_length++] = value;
                }

                target[target_length] = '\0';

                if (target_length == 0)
                    return;

                const char* resolved = target;

                if (target[0] != '/') {
                    resolved = nullptr;

                    for (usz index = 0; index < alias_count_; ++index) {
                        if (equal_text(aliases_[index].name, target)) {
                            resolved = aliases_[index].target;

                            break;
                        }
                    }

                    if (resolved == nullptr)
                        return;
                }

                for (usz index = 0; index < console_count_; ++index) {
                    const console_candidate& candidate = consoles_[index];

                    if (!equal_text(candidate.path, resolved))
                        continue;

                    if (candidate.base != console_.base ||
                        candidate.size < console_.minimum_size)
                        continue;

                    info_.console.base   = candidate.base;
                    info_.console.size   = candidate.size;
                    info_.console.source = virt::console_source::device_tree;

                    for (u32 character = 0; character < console_path_size; ++character) {
                        info_.console.path[character] = candidate.path[character];

                        if (candidate.path[character] == '\0')
                            break;
                    }

                    return;
                }
            }

            const u8*         blob_   = nullptr;
            u32               window_ = 0;
            console_policy    console_;
            virt::boot_info&  info_;
            header            header_{};
            node_frame        frames_[max_depth]{};
            u32               depth_       = 0;
            u32               path_length_ = 0;
            char              path_[console_path_size]{};
            alias_entry       aliases_[max_aliases]{};
            usz               alias_count_ = 0;
            console_candidate consoles_[max_console_nodes]{};
            usz               console_count_ = 0;
            char              stdout_path_[console_path_size]{};
            bool              has_stdout_        = false;
            bool              root_seen_         = false;
            bool              has_memory_        = false;
            u32               reservation_limit_ = 0;
        };
    } // namespace

    const char* describe(error value) {
        switch (value) {
        case error::none:
            return "none";
        case error::bad_magic:
            return "bad-magic";
        case error::too_small:
            return "too-small";
        case error::bad_version:
            return "bad-version";
        case error::bad_bounds:
            return "bad-bounds";
        case error::bad_alignment:
            return "bad-alignment";
        case error::bad_structure:
            return "bad-structure";
        case error::bad_token:
            return "bad-token";
        case error::depth_overflow:
            return "depth-overflow";
        case error::bad_string:
            return "bad-string";
        case error::bad_name:
            return "bad-name";
        case error::bad_cells:
            return "bad-cells";
        case error::too_many_regions:
            return "too-many-regions";
        case error::no_memory:
            return "no-memory";
        case error::bad_gic:
            return "bad-gic";
        }

        return "unknown";
    }

    error parse(const u8* blob, u32 window_size, console_policy console,
                virt::boot_info& info) {
        if (blob == nullptr)
            return error::bad_bounds;

        info = {};

        parser instance{blob, window_size, console, info};

        return instance.run();
    }
} // namespace astia::boot::aarch64::fdt
