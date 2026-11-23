// A module that needs the port-owned context types must compile from the
// module include set: the generic kernel include root plus the selected port
// overlay.  Nothing here names a register or a concrete layout.

#include <astia/arch/context.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/module.hpp>

extern "C" astia::u64 astia_module_arch_context_probe(
    const astia::arch::user_context& image, astia::arch::syscall_return& result,
    const astia::arch::interrupt_frame* frame) {
    result.set_mode(astia::arch::return_mode::ordinary);
    return image.stack_pointer() + astia::arch::signal_context_bytes() +
           astia::arch::max_signal_context_bytes +
           astia::arch::interrupt_stack_pointer(frame);
}
