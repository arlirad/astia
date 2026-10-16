#pragma once

#include <astia/common.hpp>

// The async operation seam: one in-flight I/O request and the state a
// device needs to resume it.
//
// The ring engine owns submission, parking and completion; the device owns
// the bytes.  `fs::file`/`dev::device::submit()` is the single ring-facing
// entry point, and its return convention is the whole protocol:
//
//   > 0   satisfied: that many bytes moved, the operation is done
//     0   not ready: the request is parked, and the device keeps its
//         reference until the engine resumes it
//   < 0   -errno: done with an error
//
// A request is a *stable* object for its whole flight.  The engine builds
// it inside the parked-operation slot and hands the same reference back on
// every resume, so a device may stash device_state (a DMA descriptor, a
// sector list, a controller tag) and expect to find it again.  That is the
// property a retry-by-signature contract cannot offer: `read(buf, len, off)`
// identifies an operation only by its arguments, and two parked reads on the
// same descriptor are indistinguishable.
//
// stage is the device's own continuation counter.  It is 0 at first
// submission and whatever the device set before parking, so a multi-step
// operation (program the controller -> move the data -> fix up metadata)
// resumes at the right step instead of re-deriving its position from
// whatever the signature happens to expose.
//
// Nothing here blocks.  A device that parks is resumed from IRQ context; a
// device whose work depends on something else (a filesystem waiting on a
// block layer) blocks a *thread*, never the device.

namespace astia {

    enum class io_direction : u8 {
        input,  // fill request.buffer() from the object
        output, // drain request.buffer() into the object
    };

    class io_request {
        public:
        io_request() = default;

        io_request(void* buffer, u32 length, u64 offset, u64 user_data,
                   io_direction direction)
            : _buffer(buffer), _length(length), _offset(offset), _user_data(user_data),
              _direction(direction) {}

        void* buffer() const {
            return _buffer;
        }
        u32 length() const {
            return _length;
        }
        u64 offset() const {
            return _offset;
        }
        u64 user_data() const {
            return _user_data;
        }
        io_direction direction() const {
            return _direction;
        }

        void offset(u64 value) {
            _offset = value;
        }

        /// Device continuation point: set before parking, read on resume.
        u8 stage() const {
            return _stage;
        }
        void stage(u8 value) {
            _stage = value;
        }

        /// Device-private, and safe to hold across a park — unlike anything
        /// on the submitting stack, which is long gone by resume time.
        void* device_state() const {
            return _device_state;
        }
        void device_state(void* value) {
            _device_state = value;
        }

        private:
        void*        _buffer       = nullptr;
        u32          _length       = 0;
        u64          _offset       = 0;
        u64          _user_data    = 0;
        void*        _device_state = nullptr;
        io_direction _direction    = io_direction::input;
        u8           _stage        = 0;
    };

} // namespace astia
