#pragma once

#include <astia/common.hpp>

namespace astia::rtl8139 {
    /// Kernel seam for one legacy INTx claim. `unregister_line` returns false
    /// while a dispatch is in flight; the kernel then keeps the handler table
    /// entry, so the claim must stay owned and the teardown must be retried.
    struct intx_ops {
        bool (*unregister_line)(u8 line);
        bool (*mask_line)(u8 line);
        void (*report_deferred)(u8 line);
    };

    /// Ownership record for one registered INTx handler. The line is masked
    /// before the unregister attempt, and the record is only cleared once the
    /// kernel confirms the handler table entry is gone.
    class intx_claim {
        public:
        bool claim(u8 line) {
            if (line > 15)
                return false;

            _line   = line;
            _active = true;

            return true;
        }

        bool release(const intx_ops& ops) {
            if (!_active)
                return true;

            if (ops.mask_line != nullptr)
                (void) ops.mask_line(_line);

            if (ops.unregister_line == nullptr || !ops.unregister_line(_line)) {
                if (ops.report_deferred != nullptr)
                    ops.report_deferred(_line);

                return false;
            }

            _line   = 0;
            _active = false;

            return true;
        }

        bool active() const {
            return _active;
        }

        u8 line() const {
            return _line;
        }

        private:
        u8   _line   = 0;
        bool _active = false;
    };
} // namespace astia::rtl8139
