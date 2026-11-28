#include "ext4_dir.hpp"
#include "ext4_extent.hpp"
#include "ext4_journal.hpp"

#include <cstdio>
#include <cstring>

namespace astia::fs {
    bool realtime_now(u64& sec, u64& nsec) {
        sec  = 1700000200;
        nsec = 0;

        return true;
    }
} // namespace astia::fs

namespace {
    using astia::core::be;
    using astia::ext4::device_io;
    using astia::ext4::group_descriptor;
    using astia::ext4::inode_record;
    using astia::ext4::journal;
    using astia::u16;
    using astia::u32;
    using astia::u64;
    using astia::u8;
    using astia::usz;

    constexpr u32 test_block_size   = 1024;
    constexpr u32 journal_inode     = 8;
    constexpr u32 target_size       = 128;
    constexpr u32 target_mtime      = 0x5555AAAA;
    constexpr char payload[]        = "journal replay payload\n";

    struct host_device {
        std::FILE* file = nullptr;
        u64        size = 0;
    };

    bool host_read(host_device& device, u64 offset, void* buffer, usz length) {
        if (device.file == nullptr || offset > device.size ||
            length > device.size - offset)
            return false;

        if (std::fseek(device.file, static_cast<long>(offset), SEEK_SET) != 0)
            return false;

        if (length == 0)
            return true;

        return std::fread(buffer, 1, length, device.file) == length;
    }

    bool host_write(host_device& device, u64 offset, const void* buffer,
                    usz length) {
        if (device.file == nullptr || offset > device.size ||
            length > device.size - offset)
            return false;

        if (std::fseek(device.file, static_cast<long>(offset), SEEK_SET) != 0)
            return false;

        if (length == 0)
            return true;

        if (std::fwrite(buffer, 1, length, device.file) != length)
            return false;

        return std::fflush(device.file) == 0;
    }

    astia::core::task<astia::i64> device_read(void* context, void* buffer,
                                              u32 length, u64 offset,
                                              astia::core::cltoken) {
        auto& device = *static_cast<host_device*>(context);

        co_return host_read(device, offset, buffer, length)
                      ? static_cast<astia::i64>(length)
                      : static_cast<astia::i64>(-EIO);
    }

    astia::core::task<astia::i64> device_write(void* context, const void* buffer,
                                               u32 length, u64 offset,
                                              astia::core::cltoken) {
        auto& device = *static_cast<host_device*>(context);

        co_return host_write(device, offset, buffer, length)
                      ? static_cast<astia::i64>(length)
                      : static_cast<astia::i64>(-EIO);
    }

    bool check(bool condition, const char* description) {
        if (!condition)
            std::printf("FAIL: %s\n", description);

        return condition;
    }

    bool copy_bytes_file(const char* input, const char* output) {
        if (std::strcmp(input, output) == 0)
            return true;

        std::FILE* source = std::fopen(input, "rb");

        if (source == nullptr)
            return false;

        std::FILE* destination = std::fopen(output, "wb");

        if (destination == nullptr) {
            std::fclose(source);

            return false;
        }

        u8  buffer[4096];
        usz amount = 0;

        while ((amount = std::fread(buffer, 1, sizeof buffer, source)) != 0) {
            if (std::fwrite(buffer, 1, amount, destination) != amount) {
                std::fclose(source);
                std::fclose(destination);

                return false;
            }
        }

        const bool read_error = std::ferror(source) != 0;

        std::fclose(source);

        if (std::fclose(destination) != 0 || read_error)
            return false;

        return true;
    }

    bool find_target(astia::ext4::volume& v, u32& number,
                     inode_record& node) {
        inode_record root;
        inode_record etc;

        u32 found = 0;
        u8  type  = 0;

        if (!v.read_inode(2, root) ||
            !astia::ext4::directory::find(v, root, "etc", found, type) ||
            !v.read_inode(found, etc) ||
            !astia::ext4::directory::find(v, etc, "known.txt", found, type) ||
            !v.read_inode(found, node))
            return false;

        number = found;

        return true;
    }

    bool inode_location(astia::ext4::volume& v, const inode_record& node,
                        u64& block, u32& offset) {
        const u32 group = (node.number - 1) / v.inodes_per_group();
        const u32 index = (node.number - 1) % v.inodes_per_group();

        group_descriptor descriptor;

        if (!v.read_group(group, descriptor))
            return false;

        const u64 table = descriptor.inode_table();

        if (table == 0)
            return false;

        const u64 within = static_cast<u64>(index) * v.inode_size();

        block  = table + within / v.block_size();
        offset = static_cast<u32>(within % v.block_size());

        return offset + v.inode_size() <= v.block_size();
    }

    bool enable_journal_checksum(astia::ext4::volume& v) {
        inode_record node;

        if (!v.read_inode(journal_inode, node))
            return false;

        u64 physical = 0;

        if (!astia::ext4::extent::map(v, node, 0, physical) || physical == 0)
            return false;

        u8 block[4096];

        if (!v.read_block(physical, block))
            return false;

        const u32 incompat = be<u32>::load(block + 0x28);

        if ((incompat & astia::ext4::journal_incompat_csum_v3) != 0)
            return true;

        be<u32>(incompat | astia::ext4::journal_incompat_csum_v3)
            .store(block + 0x28);
        block[0x50] = astia::ext4::journal_checksum_crc32c;

        be<u32>(0).store(block + 0xFC);

        const u32 checksum =
            astia::ext4::crc32c(~u32{0}, block, v.block_size());

        be<u32>(checksum).store(block + 0xFC);

        return v.write_block(physical, block);
    }

    void build_content(u8* block) {
        for (u32 i = 0; i < test_block_size; ++i) {
            block[i] = 0;
        }

        for (usz i = 0; i < sizeof payload - 1; ++i) {
            block[i] = static_cast<u8>(payload[i]);
        }
    }

    bool stage_transaction(astia::ext4::volume& v, bool crash) {
        inode_record node;
        u32          number = 0;

        if (!check(find_target(v, number, node), "locate fixture file"))
            return false;

        u64 data_block = 0;

        if (!check(astia::ext4::extent::map(v, node, 0, data_block) &&
                       data_block != 0,
                   "map fixture data block"))
            return false;

        u64 inode_block = 0;
        u32 inode_offset = 0;

        if (!check(inode_location(v, node, inode_block, inode_offset),
                   "locate fixture inode block"))
            return false;

        u8 inode_bytes[4096];

        if (!check(v.read_block(inode_block, inode_bytes),
                   "read fixture inode block"))
            return false;

        node.set_size(target_size);
        node.set_mtime(target_mtime);
        node.compute_checksum(v.checksum_seed());

        for (u32 i = 0; i < v.inode_size(); ++i) {
            inode_bytes[inode_offset + i] = node.raw[i];
        }

        u8 stale[4096];

        for (u32 i = 0; i < sizeof stale; ++i) {
            stale[i] = 0xA5;
        }

        u8 content[4096];

        build_content(content);

        if (!check(enable_journal_checksum(v), "enable journal checksums"))
            return false;

        journal record;

        if (!check(record.open(v, journal_inode), "open journal") ||
            !check(record.begin(v), "begin transaction") ||
            !check(record.stage(v, data_block, stale),
                   "stage stale data block") ||
            !check(record.stage(v, data_block, content), "stage data block") ||
            !check(record.stage(v, inode_block, inode_bytes),
                   "stage inode block"))
            return false;

        if (crash)
            journal::test_set_crash_after_commit(true);

        return check(record.commit(v), "commit transaction");
    }

    bool verify_target(astia::ext4::volume& v) {
        inode_record node;
        u32          number = 0;

        if (!check(find_target(v, number, node), "locate replayed file") ||
            !check(node.size() == target_size, "replayed size is 128") ||
            !check(node.mtime() == target_mtime, "replayed mtime"))
            return false;

        u64 data_block = 0;

        if (!check(astia::ext4::extent::map(v, node, 0, data_block) &&
                       data_block != 0,
                   "map replayed data block"))
            return false;

        u8 block[4096];

        if (!check(v.read_block(data_block, block), "read replayed block"))
            return false;

        u8 expected[4096];

        build_content(expected);

        for (u32 i = 0; i < target_size; ++i) {
            if (!check(block[i] == expected[i], "replayed content byte"))
                return false;
        }

        return true;
    }
} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::printf("usage: %s MODE INPUT OUTPUT\n", argv[0]);

        return 2;
    }

    const char* mode   = argv[1];
    const char* input  = argv[2];
    const char* output = argv[3];

    if (!copy_bytes_file(input, output)) {
        std::printf("FAIL: copy image\n");

        return 1;
    }

    host_device device;

    device.file = std::fopen(output, "r+b");

    if (device.file == nullptr) {
        std::printf("FAIL: open image\n");

        return 1;
    }

    if (std::fseek(device.file, 0, SEEK_END) != 0) {
        std::printf("FAIL: size image\n");

        return 1;
    }

    const long image_end = std::ftell(device.file);

    if (image_end < 0) {
        std::printf("FAIL: size image\n");

        return 1;
    }

    device.size = static_cast<u64>(image_end);

    device_io io{device_read, device_write, nullptr, &device, device.size,
                 test_block_size};

    astia::ext4::volume v;

    if (!check(v.open(io, true), "open volume") ||
        !check(v.block_size() == test_block_size, "block size is 1024"))
        return 1;

    bool ok = false;

    if (std::strcmp(mode, "--commit") == 0) {
        ok = stage_transaction(v, false) && check(v.sync(), "sync volume");
    }
    else if (std::strcmp(mode, "--crash") == 0) {
        ok = stage_transaction(v, true);
    }
    else if (std::strcmp(mode, "--recover") == 0) {
        journal record;

        ok = check(record.open(v, journal_inode), "open journal") &&
             check(record.requires_recovery(), "journal requires recovery") &&
             check(record.recover(v), "recover journal") &&
             check(!record.requires_recovery(), "journal is clean") &&
             verify_target(v) && check(v.sync(), "sync recovered volume");
    }
    else if (std::strcmp(mode, "--revoke-reset") == 0) {
        journal record;

        constexpr u64 revoked_block    = 0x1234;
        constexpr u32 revoke_sequence  = 2;

        ok = check(record.test_add_revoke(revoked_block, revoke_sequence),
                   "insert test revoke") &&
             check(record.test_revoked(revoked_block, 1),
                   "revoke suppresses an earlier transaction") &&
             check(record.test_revoked(revoked_block, revoke_sequence),
                   "revoke suppresses its own transaction") &&
             check(!record.test_revoked(revoked_block, revoke_sequence + 1),
                   "revoke does not suppress a later transaction");

        record.test_reset_replay();

        ok = ok &&
             check(record.test_revoked(revoked_block, revoke_sequence),
                   "revoke survives the recovery replay reset") &&
             check(!record.test_revoked(revoked_block, revoke_sequence + 1),
                   "later transaction still replays after the reset");
    }
    else {
        std::printf("FAIL: unknown mode %s\n", mode);

        return 2;
    }

    if (std::fclose(device.file) != 0)
        ok = false;

    if (!ok)
        return 1;

    std::printf("OK\n");

    return 0;
}
