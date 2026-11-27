#include "smp.hpp"

#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/arch/layout.hpp>
#include <astia/arch/page_map.hpp>
#include <astia/arch/platform.hpp>
#include <astia/atomic.hpp>
#include <astia/log.hpp>
#include <astia/mm/heap.hpp>

#include "../../acpi/limits.hpp"
#include "../../acpi/madt.hpp"
#include "../../acpi/reader.hpp"
#include "../../proc/scheduler.hpp"
#include "gdt.hpp"
#include "lapic.hpp"
#include "pci_mmio_policy.hpp"

extern "C" {
extern volatile astia::u32 astia_ap_cr3;
extern volatile astia::u64 astia_ap_stack, astia_ap_entry, astia_ap_cpu;
extern void                astia_ap_trampoline_start();
extern void                astia_ap_trampoline_end();
}

namespace astia::arch::x86_64::smp {
    namespace {
        using logs = astia::log::context<"smp">;

        constexpr u32 max_cpus = platform::max_cpu_count;

        u32 cpu_total        = 1;
        u32 cpu_online       = 1;
        u32 scheduler_online = 0;
        u32 apic_ids[max_cpus]{};
        alignas(cache_line_bytes) u32 reschedule_pending[max_cpus]{};

        extern "C" void ap_entry(u64 cpu_index) {
            logs::info("AP%u entered trampoline", static_cast<u32>(cpu_index));

            if (!x86_64::init_gdt_tss_cpu(static_cast<u32>(cpu_index))) {
                logs::error("AP%u per-CPU GDT/TSS allocation failed",
                            static_cast<u32>(cpu_index));

                return;
            }

            astia::arch::load_interrupts();

            if (!lapic::enable()) {
                logs::error("AP%u LAPIC enable failed", static_cast<u32>(cpu_index));

                return;
            }

            astia::atomic_ref<u32>(cpu_online).fetch_add(1, astia::memory_order::release);
            logs::info("AP%u online", static_cast<u32>(cpu_index));
            astia::arch::cpu::interrupts();
            logs::info("AP%u park", static_cast<u32>(cpu_index));

            while (astia::atomic_ref<u32>(scheduler_online)
                       .load(astia::memory_order::acquire) == 0) {
                astia::arch::cpu::halt();
            }

            if (!astia::proc::scheduler::start_cpu(static_cast<u32>(cpu_index))) {
                logs::error("AP%u scheduler start failed", static_cast<u32>(cpu_index));

                return;
            }

            while (true) {
                astia::arch::cpu::halt();

                if (consume_reschedule(static_cast<u32>(cpu_index)))
                    astia::proc::scheduler::yield();
            }
        }

        u32 cpuid_threads() {
            u32 eax = 1, ebx = 0, ecx = 0, edx = 0;

            asm volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));

            const u32 count = (ebx >> 16) & 0xffu;

            return count == 0 ? 1 : count;
        }
    } // namespace

    bool init(const boot::boot_info& info) {
        cpu_total        = 1;
        cpu_online       = 1;
        scheduler_online = 0;

        astia::atomic_ref<u32>(reschedule_pending[0])
            .store(0u, astia::memory_order::relaxed);

        bool have_acpi = info.rsdp != 0;
        if (!have_acpi) {
            cpu_total = platform::clamp_cpu_count(cpuid_threads());

            logs::info("discovered=%u online=1 (CPUID; no ACPI RSDP)", cpu_total);
            logs::info("reschedule IPI armed");

            if (cpu_total > 2) {
                logs::error(
                    "AP startup unavailable: no APIC-ID topology (safe limit is 1 AP)");

                return true;
            }
        }

        u32 found = 0;

        if (have_acpi) {
            u64 direct_limit = 0;

            astia::acpi::detail::madt_error madt_error =
                astia::acpi::detail::madt_error::none;

            if (astia::acpi::detail::direct_map_limit(info.memory, direct_limit)) {
                astia::acpi::detail::physical_reader context{
                    pci_identity_window_size,
                    direct_limit,
                };

                const auto access = astia::acpi::detail::physical_memory_reader(context);

                if (!astia::acpi::detail::discover_madt(access, info.rsdp, apic_ids,
                                                        max_cpus, found, madt_error))
                    found = 0;
            }

            if (found == 0 && madt_error == astia::acpi::detail::madt_error::bad_rsdp) {
                have_acpi = false;
                cpu_total = platform::clamp_cpu_count(cpuid_threads());

                logs::info("discovered=%u online=1 (CPUID; invalid ACPI RSDP)",
                           cpu_total);
                logs::info("reschedule IPI armed");

                if (cpu_total > 1) {
                    logs::info(
                        "AP startup deferred: no APIC-ID topology (invalid ACPI RSDP)");

                    return true;
                }
            }
        }

        if (have_acpi && found == 0) {
            have_acpi = false;
            cpu_total = platform::clamp_cpu_count(cpuid_threads());
        }

        const u32 bsp = lapic::id();

        if (found != 0) {
            bool have_bsp = false;
            bool have_ap  = false;

            for (u32 i = 0; i < found; ++i) {
                if (apic_ids[i] == bsp)
                    have_bsp = true;
                else
                    have_ap = true;
            }

            if (!have_bsp || !have_ap)
                have_acpi = false;
        }

        if (found != 0)
            cpu_total = platform::clamp_cpu_count(found);

        logs::info("discovered=%u online=1", cpu_total);

        if (cpu_total <= 1)
            return true;

        if (!have_acpi) {
            logs::info(
                found == 0 && info.rsdp != 0
                    ? "AP startup deferred: no APIC-ID topology (invalid/missing MADT)"
                    : "AP startup deferred: no APIC-ID topology (PVH provides no ACPI "
                      "RSDP)");
            logs::info("reschedule IPI armed");

            return true;
        }

        const bool broadcast = !have_acpi;
        u32        index     = 1;

        for (u32 n = 0; n < cpu_total; ++n) {
            if ((!broadcast && apic_ids[n] == bsp) || (broadcast && n != 0))
                continue;

            constexpr usz ap_stack_bytes = 16 * 1024;

            auto stack = mm::heap::alloc(ap_stack_bytes);
            if (!stack) {
                logs::error("AP%u stack allocation failed", index);
                break;
            }

            astia_ap_cr3   = static_cast<u32>(astia::arch::kernel_page_map()->root());
            astia_ap_stack = reinterpret_cast<u64>(stack.value) + ap_stack_bytes;
            astia_ap_entry = reinterpret_cast<u64>(&ap_entry);
            astia_ap_cpu   = index;

            const bool sent = broadcast ? lapic::init_sipi_all(0x08)
                                        : lapic::init_sipi(apic_ids[n], 0x08);
            if (!sent) {
                logs::error("APIC INIT/SIPI failed for APIC ID %u", apic_ids[n]);
                break;
            }

            for (u32 wait = 0;
                 wait < 1000000 &&
                 astia::atomic_ref<u32>(cpu_online).load(astia::memory_order::acquire) <
                     index + 1;
                 ++wait) {
            }

            if (astia::atomic_ref<u32>(cpu_online).load(astia::memory_order::acquire) <
                index + 1) {
                logs::error("APIC ID %u did not report online", apic_ids[n]);
                break;
            }

            ++index;
        }

        logs::info("online=%u", cpu_online);

        if (cpu_online == cpu_total)
            logs::info("smp: verified online=%u", cpu_online);

        logs::info("reschedule IPI armed");
        request_reschedule();

        return true;
    }

    bool init_scheduler() {
        // AP entry must not wait for proc: APs finish architectural bring-up
        // and park, then the BSP publishes their scheduler state here.
        const u32 online =
            astia::atomic_ref<u32>(cpu_online).load(astia::memory_order::acquire);

        for (u32 index = 1; index < online && index < max_cpus; ++index) {
            if (!astia::proc::scheduler::init_cpu(index)) {
                logs::error("AP%u scheduler initialization failed", index);

                return false;
            }
        }

        astia::atomic_ref<u32>(scheduler_online).store(1u, astia::memory_order::release);

        if (astia::atomic_ref<u32>(cpu_online).load(astia::memory_order::acquire) > 1)
            request_reschedule();

        return true;
    }

    u32 online_cpus() {
        return cpu_online;
    }

    void request_reschedule() {
        request_local_reschedule();

        const u32 online =
            astia::atomic_ref<u32>(cpu_online).load(astia::memory_order::acquire);

        for (u32 index = 1; index < online && index < max_cpus; ++index) {
            astia::atomic_ref<u32>(reschedule_pending[index])
                .store(1u, astia::memory_order::release);
            (void) lapic::send_ipi(apic_ids[index], lapic::reschedule_vector);
        }
    }

    void request_local_reschedule() {
        const u32 index = astia::arch::cpu::current_index();
        if (index < max_cpus)
            astia::atomic_ref<u32>(reschedule_pending[index])
                .store(1u, astia::memory_order::release);
    }

    // Called by the vector-49 epilogue while the interrupt gate is still
    // closed.  Keep the interrupt handler itself to flagging and EOI only.
    void request_local_reschedule_from_interrupt() {
        request_local_reschedule();
    }

    bool consume_reschedule(u32 cpu_index) {
        if (cpu_index >= max_cpus)
            return false;

        return astia::atomic_ref<u32>(reschedule_pending[cpu_index])
                   .exchange(0u, astia::memory_order::acq_rel) != 0;
    }
} // namespace astia::arch::x86_64::smp
