#include "path_service.hpp"

#include <astia/api/errno.h>
#include <astia/mm/heap.hpp>
#include <astia/strings.hpp>

#include <new>

namespace astia::fs::path_service {
    namespace {
        long copy_string(char* destination, usz capacity, const char* source,
                         long too_small) {
            if (destination == nullptr || source == nullptr || capacity == 0)
                return -EINVAL;

            usz length = 0;

            while (source[length] != '\0') {
                ++length;
            }

            if (length + 1 > capacity)
                return too_small;

            for (usz i = 0; i <= length; ++i) {
                destination[i] = source[i];
            }

            return 0;
        }

        u32 length_of(const char* value) {
            u32 length = 0;

            while (value != nullptr && value[length] != '\0') {
                ++length;
            }

            return length;
        }

        struct resolve_scratch {
            char pending[path_max * 2];
            char result[path_max];
            char component[path_max];
            char target[path_max];
            char combined[path_max * 2];
        };

        class resolve_scratch_owner {
            public:
            explicit resolve_scratch_owner(resolve_scratch* value) : _value(value) {}

            ~resolve_scratch_owner() {
                if (_value != nullptr)
                    mm::heap::free(_value);
            }

            resolve_scratch_owner(const resolve_scratch_owner&)            = delete;
            resolve_scratch_owner& operator=(const resolve_scratch_owner&) = delete;

            resolve_scratch* get() const {
                return _value;
            }

            private:
            resolve_scratch* _value;
        };
    }

    long compose(const char* base, const char* path, span<char> buffer) {
        if (base == nullptr || path == nullptr || buffer.null() || buffer.empty())
            return -EINVAL;

        if (path[0] == '/')
            return copy_string(buffer.data(), buffer.size(), path, -ENAMETOOLONG);

        if (path[0] == '.' && path[1] == '\0')
            return copy_string(buffer.data(), buffer.size(), base, -ERANGE);

        char candidate[path_max]{};
        usz  at = 0;

        for (; base[at] != '\0' && at + 1 < sizeof candidate; ++at) {
            candidate[at] = base[at];
        }

        if (base[at] != '\0')
            return -ENAMETOOLONG;

        if (at > 1 && at + 1 < sizeof candidate)
            candidate[at++] = '/';

        usz source = 0;

        for (; path[source] != '\0' && at + 1 < sizeof candidate; ++source, ++at) {
            candidate[at] = path[source];
        }

        if (path[source] != '\0')
            return -ENAMETOOLONG;

        candidate[at] = '\0';

        return copy_string(buffer.data(), buffer.size(), candidate, -ENAMETOOLONG);
    }

    bool within_root(const char* root, const char* path) {
        if (root == nullptr || path == nullptr || root[0] != '/' || path[0] != '/')
            return false;

        if (root[1] == '\0')
            return true;

        u32 index = 0;

        for (; root[index] != '\0'; ++index) {
            if (path[index] != root[index])
                return false;
        }

        return path[index] == '\0' || path[index] == '/';
    }

    long resolve(const char* path, char* buffer, u32 capacity, bool follow_final) {
        return resolve_from("/", path, buffer, capacity, follow_final);
    }

    long resolve_from(const char* root, const char* path, char* buffer, u32 capacity,
                      bool follow_final) {
        if (root == nullptr || root[0] != '/' || path == nullptr || buffer == nullptr ||
            capacity == 0 || path[0] != '/')
            return -EINVAL;

        const u32 root_length = length_of(root);

        if (root_length == 0 || root_length >= path_max ||
            (root_length > 1 && root[root_length - 1] == '/'))
            return -EINVAL;

        auto memory = mm::heap::alloc(sizeof(resolve_scratch));
        if (!memory)
            return -ENOMEM;

        auto* storage = ::new (static_cast<void*>(memory.value)) resolve_scratch{};
        resolve_scratch_owner owner(storage);

        char(&pending)[path_max * 2]  = storage->pending;
        char(&result)[path_max]       = storage->result;
        char(&component)[path_max]    = storage->component;
        char(&target)[path_max]       = storage->target;
        char(&combined)[path_max * 2] = storage->combined;

        u32 plen = 0, rlen = root_length, links = 0;
        u32 path_length = 0;

        while (path[path_length] != '\0') {
            ++path_length;
        }

        const bool trailing_slash = path_length != 0 && path[path_length - 1] == '/';

        for (u32 i = 0; i < root_length; ++i) {
            result[i] = root[i];
        }

        result[root_length] = '\0';

        for (u32 i = 0; path[i] != '\0'; ++i) {
            if (plen + 1 >= sizeof pending)
                return -ENAMETOOLONG;

            pending[plen++] = path[i];
        }

        pending[plen] = '\0';

        while (true) {
            u32 start = 0;

            while (start < plen && pending[start] == '/') {
                ++start;
            }

            if (start == plen)
                break;

            u32 end = start;

            while (end < plen && pending[end] != '/') {
                ++end;
            }

            u32 clen = end - start;
            if (clen >= path_max)
                return -ENAMETOOLONG;

            for (u32 i = 0; i < clen; ++i) {
                component[i] = pending[start + i];
            }

            component[clen] = '\0';

            for (u32 i = end; i <= plen; ++i) {
                pending[i - end] = pending[i];
            }

            plen -= end;

            while (plen != 0 && pending[0] == '/') {
                for (u32 i = 0; i < plen; ++i) {
                    pending[i] = pending[i + 1];
                }

                --plen;
            }

            if (string_equal(component, "."))
                continue;

            if (string_equal(component, "..")) {
                if (rlen > root_length) {
                    --rlen;

                    while (rlen > root_length && result[rlen - 1] != '/') {
                        --rlen;
                    }

                    // Drop the separator too: the parent of "/tmp/foo" is
                    // "/tmp", not "/tmp/". Leaving the slash makes the next
                    // component resolve as "//x" and the parent check fail
                    // against filesystems that match paths exactly.
                    if (rlen > root_length && result[rlen - 1] == '/')
                        --rlen;

                    result[rlen] = '\0';
                }

                continue;
            }

            if (rlen > 1 && node_type(result) != directory_dir)
                return node_type(result) == directory_unknown ? -ENOENT : -ENOTDIR;

            const u32 parent_len = rlen;

            if (rlen > 1)
                result[rlen++] = '/';

            if (rlen + clen >= sizeof result)
                return -ENAMETOOLONG;

            for (u32 i = 0; i < clen; ++i) {
                result[rlen++] = component[i];
            }

            result[rlen] = '\0';

            const bool last = plen == 0;

            if ((!last || follow_final) && node_type(result) == directory_lnk) {
                if (++links > 16)
                    return -ELOOP;

                long n = read_symlink(result, target, sizeof target);
                if (n < 0)
                    return n;

                const u32 target_len = length_of(target);
                if (target_len == 0)
                    return -ENOENT;

                u32 tail = 0;

                while (pending[tail] != '\0') {
                    ++tail;
                }

                const u32 combined_len = target_len + (tail ? tail + 1 : 0);

                if (combined_len >= sizeof combined)
                    return -ENAMETOOLONG;

                for (u32 i = 0; i < target_len; ++i) {
                    combined[i] = target[i];
                }

                if (tail != 0) {
                    combined[target_len] = '/';

                    for (u32 i = 0; i <= tail; ++i) {
                        combined[target_len + 1 + i] = pending[i];
                    }
                }
                else {
                    combined[combined_len] = '\0';
                }

                plen = combined_len;

                for (u32 i = 0; i <= plen; ++i) {
                    pending[i] = combined[i];
                }

                if (target[0] == '/') {
                    rlen = root_length;

                    for (u32 i = 0; i < root_length; ++i) {
                        result[i] = root[i];
                    }
                }
                else {
                    rlen = parent_len;
                }

                result[rlen] = '\0';
            }
        }

        if (trailing_slash) {
            const file_type final_type = node_type(result);
            if (final_type == directory_unknown)
                return -ENOENT;

            if (final_type != directory_dir)
                return -ENOTDIR;
        }

        if (rlen + 1 > capacity)
            return -ENAMETOOLONG;

        for (u32 i = 0; i <= rlen; ++i) {
            buffer[i] = result[i];
        }

        return 0;
    }
}
