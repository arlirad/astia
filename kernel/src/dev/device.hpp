#pragma once

#include <astia/common.hpp>

// A logical device: the root of the blk/char/net split and the seam
// the async ring engine binds to.  aretev-shaped — a name, a kind, and
// virtual operations — with the iopkt machinery replaced by the ring:
// the engine owns parking and completion, the device just moves bytes
// and calls sys::io_device_ready() when it may have more.

namespace astia::dev {

    /// Device kind and behavior, bit for bit as aretev's dflags: the
    /// class bits compose — a tty is a char device with terminal
    /// semantics, a net device a char device with packet semantics.
    enum device_flags : unsigned {
        dv_sync     = 0b000000001, // completes ops synchronously
        dv_mqueue   = 0b000000010, // has request queues
        dv_buffered = 0b000000100, // allows buffered IO
        dv_char     = 0b000100000,
        dv_block    = 0b001000000,
        dv_net      = 0b010100000,
        dv_tty      = 0b100100000,
    };

    /// Hardware/device abstraction.  Filesystem exposure is deliberately
    /// a separate adapter (`device_file`), so hardware objects do not
    /// become filesystem objects by inheritance.
    class device {
        public:
        constexpr device(const char* name, unsigned flags) : _flags(flags) {
            u32 i = 0;
            while (name != nullptr && name[i] != '\0' && i < sizeof(_name) - 1) {
                _name[i] = name[i];
                ++i;
            }
            _name[i] = '\0';
        }

        const char* name() const {
            return _name;
        }

        unsigned flags() const {
            return _flags;
        }

        // Ring-facing I/O, same contract as sys::io_device: read
        // returns bytes taken (0 parks the op), write bytes sent
        // (short is legal); both return -errno on failure.  off is the
        // SQE's offset — meaningless for a stream, the position for a
        // disk.  The engine may call these from IRQ context; neither
        // may block.
        virtual long read(void* buf, u32 len, u64 off)        = 0;
        virtual long write(const void* buf, u32 len, u64 off) = 0;

        // Device-specific control: TCGETS/TCSETS on a tty, geometry on
        // a disk.  -ENOTTY when the request is unknown.
        virtual long ioctl(u64 request, void* argp);

        protected:
        char _name[16]{}; // {} matters: an uninitialized array member
                          // makes the type non-literal (constant
                          // initialization rejects writes into it)
        unsigned _flags;
    };

} // namespace astia::dev
