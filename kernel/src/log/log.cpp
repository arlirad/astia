#include <astia/api/syscalls.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/log.hpp>

#include "proc.hpp"

namespace astia::log::detail {

    namespace {
        inline constexpr u32 pending_capacity = 4096;
        inline constexpr u32 panic_lock_spins = 4096;

        char           pending[2][pending_capacity];
        u32            pending_count[2]{};
        core::spinlock record_lock;

        u32 pending_slot(level severity) {
            return severity == level::info ? 0 : 1;
        }

        /// A number right-aligned in `width` columns, padded on the left
        /// with `pad`.
        void field(record_builder& record, u64 value, u32 width, char pad) {
            char digits[20];
            u32  count = 0;
            do {
                digits[count++] = static_cast<char>('0' + value % 10);
                value /= 10;
            } while (value != 0 && count < sizeof(digits));
            for (u32 i = count; i < width; ++i) {
                record.append(pad);
            }
            while (count != 0) {
                record.append(digits[--count]);
            }
        }

        inline constexpr u32 level_width = 5;

        const char* level_name(level severity) {
            switch (severity) {
            case level::debug:
                return "DEBUG";
            case level::info:
                return "INFO";
            case level::warning:
                return "WARN";
            case level::error:
                return "ERROR";
            case level::panic:
                return "PANIC";
            }
            return "?";
        }

        /// Seconds since the timer started, and the fraction of a second the
        /// tick rate actually resolves: `  123.456789`.  Before there is a
        /// timer the clock reads zero rather than refusing to log.
        void time_field(record_builder& record) {
            const u64 hz     = arch::timer_hz();
            const u64 ticks  = arch::timer_ticks();
            const u64 micros = hz == 0 ? 0 : ticks * microseconds_per_second / hz;
            field(record, micros / microseconds_per_second, 5, ' ');
            record.append('.');
            field(record, micros % microseconds_per_second, 6, '0');
        }

        /// The severity, right-aligned in five columns: ` INFO`, `ERROR`.
        void level_field(record_builder& record, level severity) {
            const char* word   = level_name(severity);
            u32         length = 0;
            while (word[length] != '\0') {
                ++length;
            }
            for (u32 i = length; i < level_width; ++i) {
                record.append(' ');
            }
            for (u32 i = 0; i < length; ++i) {
                record.append(word[i]);
            }
        }

        void buffer(level severity, const char* data, u32 length) {
            const u32 slot = pending_slot(severity);
            for (u32 i = 0; i < length && pending_count[slot] < pending_capacity; ++i) {
                pending[slot][pending_count[slot]++] = data[i];
            }
        }

        /// The stream a record is filed under, or null when the process
        /// redirected it away from the diagnostics (or there is no process
        /// yet) and the record has to be buffered instead.
        mm::shptr<fs::open_file> stream_for(level severity) {
            proc::process* current = proc::current_process();
            auto           stream  = current == nullptr
                                         ? mm::shptr<fs::open_file>{}
                                         : current->descriptor(
                                    fd_t{severity == level::info ? STDOUT : STDERR});
            if (!stream || (severity == level::info && stream->parent() &&
                            stream->parent()->mode().is_regular())) {
                // A process can redirect stdout to a regular file (tests and
                // pipelines).  Kernel diagnostics must not be injected into that
                // payload; fall back to the serial log instead.
                return {};
            }
            return stream;
        }

        /// Flush bytes buffered by earlier short writes.  False leaves the
        /// unwritten remainder at the front of the buffer.
        bool flush_pending(fs::open_file& stream, u32 slot) {
            if (pending_count[slot] == 0) {
                return true;
            }
            const long written =
                stream.write(pending[slot], pending_count[slot], fs::offset_current);
            const u32 accepted = written > 0 ? static_cast<u32>(written) : 0;
            if (accepted != pending_count[slot]) {
                const u32 remaining = pending_count[slot] - accepted;
                for (u32 i = 0; i < remaining; ++i) {
                    pending[slot][i] = pending[slot][accepted + i];
                }
                pending_count[slot] = remaining;
                return false;
            }
            pending_count[slot] = 0;
            return true;
        }

        /// Holds the record lock with interrupts masked: an interrupt on this
        /// CPU must not preempt a holder and spin on the same lock.  A panic
        /// only waits a bounded number of spins so a crash on one CPU cannot
        /// deadlock against a log writer that never runs again; if the lock
        /// cannot be taken in time the record is written anyway and may
        /// interleave with the other writer's output.
        class record_guard {
            public:
            explicit record_guard(bool bounded) : _lock(&record_lock) {
                if (bounded) {
                    for (u32 spin = 0; spin < panic_lock_spins; ++spin) {
                        if (_lock->try_lock()) {
                            _held = true;
                            return;
                        }
                        arch::cpu::relax();
                    }
                    return;
                }
                _lock->lock();
                _held = true;
            }

            ~record_guard() {
                unlock();
            }

            record_guard(const record_guard&)            = delete;
            record_guard& operator=(const record_guard&) = delete;

            bool held() const {
                return _held;
            }

            void unlock() {
                if (_held) {
                    _lock->unlock();
                    _held = false;
                }
                _gate.unlock();
            }

            private:
            arch::interrupt_guard _gate;
            core::spinlock*       _lock;
            bool                  _held = false;
        };
    } // namespace

    void prefix(record_builder& record, level severity, const char* context, u32 length) {
        record.append('[');
        time_field(record);
        record.append(' ');
        level_field(record, severity);
        record.append(']');
        record.append(' ');
        for (u32 i = 0; i < length; ++i) {
            record.append(context[i]);
        }
        record.append(':');
        record.append(' ');
    }

    void emit(level severity, const char* data, u32 length) {
        record_guard guard{severity == level::panic};

        if (!guard.held()) {
            // Bounded panic acquisition failed: report the record directly.
            // It may interleave with another CPU's record, which is
            // preferable to a panic that never produces output, and the
            // pending buffer is left alone because it is not ours to touch.
            if (auto stream = stream_for(severity)) {
                stream->write(data, length, fs::offset_current);
            }
            return;
        }

        auto stream = stream_for(severity);
        if (!stream) {
            buffer(severity, data, length);
            return;
        }

        const u32 slot = pending_slot(severity);
        if (!flush_pending(*stream, slot)) {
            buffer(severity, data, length);
            return;
        }

        const long written  = stream->write(data, length, fs::offset_current);
        const u32  accepted = written > 0 ? static_cast<u32>(written) : 0;
        if (accepted != length) {
            buffer(severity, data + accepted, length - accepted);
        }
    }

} // namespace astia::log::detail
