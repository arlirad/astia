#include "aio.hpp"

#include <astia/atomic.hpp>

#include "../io/aio/aio.hpp"
#include "time.hpp"
#include "user_mem.hpp"

namespace {
    using adapter            = astia::io::aio::user_memory_adapter;
    astia::u32 adapter_state = 0;

    bool user_span_ok(astia::proc::process* owner, astia::span<astia::u8> range) {
        return astia::sys::user_span_ok_for(owner, range);
    }

    bool user_span_writable_ok(astia::proc::process*  owner,
                               astia::span<astia::u8> range) {
        return astia::sys::user_span_writable_ok_for(owner, range);
    }

    bool kernel_span_ok(astia::span<astia::u8> range) {
        return astia::sys::kernel_span_ok(range);
    }

    long pin_user_span(astia::proc::process* owner, astia::span<astia::u8> range,
                       astia::io::direction direction, astia::mm::iovec& out,
                       bool allow_kernel_buffer) {
        return astia::sys::pin_user_span_for(owner, range, direction, out,
                                             allow_kernel_buffer);
    }

    bool copy_in(astia::proc::process*, void* destination,
                 astia::span<astia::u8> source) {
        return astia::sys::copy_in(destination, source);
    }

    bool copy_in_string(astia::proc::process*, astia::span<char> destination,
                        astia::span<astia::u8> source) {
        return astia::sys::copy_in_string(destination, source);
    }

    bool copy_out(astia::proc::process* owner, astia::span<astia::u8> destination,
                  const void* source) {
        return astia::sys::copy_out_for(owner, destination, source);
    }

    bool realtime_now(astia::u64& sec, astia::u64& nsec) {
        return astia::sys::realtime_now(sec, nsec);
    }

    void install_user_memory_adapter() {
        astia::u32 expected = 0;

        if (astia::atomic_ref<astia::u32>(adapter_state)
                .load(astia::memory_order::acquire) == 2)
            return;

        if (!astia::atomic_ref<astia::u32>(adapter_state)
                 .compare_exchange_strong(expected, 1, astia::memory_order::acq_rel,
                                          astia::memory_order::acquire)) {
            while (astia::atomic_ref<astia::u32>(adapter_state)
                       .load(astia::memory_order::acquire) != 2) {
            }

            return;
        }

        static const adapter value{
            user_span_ok, user_span_writable_ok, kernel_span_ok, pin_user_span,
            copy_in,      copy_in_string,        copy_out,       realtime_now,
        };

        astia::io::aio::set_user_memory_adapter(value);
        astia::fs::set_realtime_source(realtime_now);
        astia::atomic_ref<astia::u32>(adapter_state)
            .store(2, astia::memory_order::release);
    }
} // namespace

namespace astia::sys {
    long io_setup(u32 entries, u32 flags, astia_rings** out) {
        install_user_memory_adapter();

        return ::astia::io::aio::io_setup(entries, flags, out);
    }

    long io_destroy(astia_rings* ring) {
        install_user_memory_adapter();

        return ::astia::io::aio::io_destroy(ring);
    }

    long io_submit(astia_rings* ring, u32 to_submit) {
        install_user_memory_adapter();

        return ::astia::io::aio::io_submit(ring, to_submit);
    }

    bool io_allow_kernel_buffers(astia_rings* ring) {
        install_user_memory_adapter();

        return ::astia::io::aio::io_allow_kernel_buffers(ring);
    }

    void io_device_ready(i32 fd) {
        ::astia::io::aio::io_device_ready(fd);
    }

    void io_device_ready_context(const io_device* dev) {
        ::astia::io::aio::io_device_ready_context(dev);
    }

    void io_device_ready_context_except(const io_device* dev, u64 excluded_user_data) {
        ::astia::io::aio::io_device_ready_context_except(dev, excluded_user_data);
    }

    void io_process_deferred_ready() {
        install_user_memory_adapter();
        ::astia::io::aio::io_process_deferred_ready();
    }

    void io_interrupt_process(proc::process* owner, i32 result) {
        install_user_memory_adapter();
        ::astia::io::aio::io_interrupt_process(owner, result);
    }

    void io_poll_tick() {
        install_user_memory_adapter();
        ::astia::io::aio::io_poll_tick();
    }

    void io_exec_process(proc::process* owner) {
        install_user_memory_adapter();
        ::astia::io::aio::io_exec_process(owner);
    }

    void io_exit_process(proc::process* owner) {
        install_user_memory_adapter();
        ::astia::io::aio::io_exit_process(owner);
    }
} // namespace astia::sys
