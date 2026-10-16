// The kernel export table: which symbols a loadable module may bind against.
//
// Pure bookkeeping — no mm dependency — so the host tests exercise it directly.

#include <astia/mod/exports.hpp>

namespace astia::mod {

    namespace {

        struct export_entry {
            const char* name;
            u64         addr;
        };

        export_entry table[max_exports] = {};
        usize        used               = 0;

        auto str_eq(const char* a, const char* b) -> bool {
            while (*a != '\0' && *a == *b) {
                ++a;
                ++b;
            }
            return *a == *b;
        }

    } // namespace

    auto expose(const char* name, u64 addr) -> bool {
        if (name == nullptr || addr == 0 || used >= max_exports) {
            return false;
        }
        for (usize i = 0; i < used; ++i) {
            if (str_eq(table[i].name, name)) {
                return false; // already exported
            }
        }
        table[used++] = {name, addr};
        return true;
    }

    auto resolve(const char* name) -> u64 {
        if (name == nullptr) {
            return 0;
        }
        for (usize i = 0; i < used; ++i) {
            if (str_eq(table[i].name, name)) {
                return table[i].addr;
            }
        }
        return 0;
    }

} // namespace astia::mod
