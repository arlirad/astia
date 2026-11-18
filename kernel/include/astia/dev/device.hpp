#pragma once

#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/common.hpp>
#include <astia/hw/driver.hpp>
#include <astia/io.hpp>
#include <astia/span.hpp>

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
    class device : public hw::product {
        public:
        constexpr device(const char* name, unsigned flags) : _flags(flags) {
            u32 i = 0;
            while (i < sizeof(_name) - 1 && name != nullptr && name[i] != '\0') {
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

        // The ring's actual entry point.  The default routes to
        // read()/write() above, which is all a device that finishes
        // inline ever needs; a device that has to wait for hardware
        // overrides this, parks the request (returns 0), and resumes it
        // by stage from its interrupt handler.  See <astia/io.hpp>.
        virtual long submit(io::request& request);
        virtual u32  poll(u32 events, u32 flags);

        // Device-specific control: TCGETS/TCSETS on a tty, geometry on
        // a disk.  -ENOTTY when the request is unknown.
        virtual long ioctl(u64 request, void* argp, usize size);
        virtual long acquire_controlling_terminal(i32 group);

        // Span adapters keep internal device call sites from carrying a
        // pointer and length separately; the virtual compatibility seam above
        // remains unchanged for file/driver implementations.
        long read(astia::span<u8> buffer, u64 off) {
            if (buffer.size() > static_cast<usize>(~u32{0}))
                return -EINVAL;
            return read(buffer.data(), static_cast<u32>(buffer.size()), off);
        }
        long write(astia::span<const u8> buffer, u64 off) {
            if (buffer.size() > static_cast<usize>(~u32{0}))
                return -EINVAL;
            return write(buffer.data(), static_cast<u32>(buffer.size()), off);
        }
        long ioctl(u64 request, astia::span<u8> argument) {
            return ioctl(request, argument.data(), argument.size());
        }

        protected:
        char     _name[16]{}; // {} matters: an uninitialized array member
                              // makes the type non-literal (constant
                              // initialization rejects writes into it)
        unsigned _flags;
    };

} // namespace astia::dev
