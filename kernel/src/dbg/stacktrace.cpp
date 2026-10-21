#include "stacktrace.hpp"

#include <astia/arch/layout.hpp>
#include <astia/log.hpp>

namespace astia::dbg {

    void stacktrace(u64 rbp, u64 rip) {
        using logs               = astia::log::context<"dbg::stack">;
        constexpr u32 max_frames = 24;
        const u64     base       = astia::arch::kernel_base;

        logs::error("#0 rip=%llx rbp=%llx", static_cast<unsigned long long>(rip),
                    static_cast<unsigned long long>(rbp));
        for (u32 depth = 1; depth < max_frames; ++depth) {
            if (rbp < base || (rbp & 0xf) != 0) {
                logs::error("stop invalid-rbp=%llx",
                            static_cast<unsigned long long>(rbp));
                return;
            }
            const auto* frame  = reinterpret_cast<const u64*>(rbp);
            const u64   next   = frame[0];
            const u64   caller = frame[1];
            logs::error("#%d rip=%llx rbp=%llx", static_cast<int>(depth),
                        static_cast<unsigned long long>(caller),
                        static_cast<unsigned long long>(next));
            if (next <= rbp || next - rbp > 1024 * 1024 || caller < base) {
                logs::error("stop broken-frame");
                return;
            }
            rbp = next;
        }
        logs::error("stop frame-limit");
    }

} // namespace astia::dbg
