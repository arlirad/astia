#include "smp.hpp"

#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/arch/layout.hpp>
#include <astia/arch/page_map.hpp>
#include <astia/arch/platform.hpp>
#include <astia/atomic.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/log.hpp>
#include <astia/mm/heap.hpp>

#include "../../acpi/madt.hpp"
#include "../../acpi/physical.hpp"
#include "../../core/panic.hpp"
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

        constexpr u32 max_cpus       = platform::max_cpu_count;
        constexpr u32 tlb_mask_words = (max_cpus + 63) / 64;

        u32 cpu_total        = 1;
        u32 cpu_online       = 1;
        u32 scheduler_online = 0;
        u32 apic_ids[max_cpus]{};
        u32 logical_apic_ids[max_cpus]{};
        alignas(cache_line_bytes) u32 reschedule_pending[max_cpus]{};

        // Pages must not be cached by a CPU while another CPU narrows their
        // permissions, so a PTE change rushes an invalidation to every CPU
        // that may hold the old translation and waits for the acknowledgement.
        //
        // One request is in flight at a time (tlb_lock). Every publication
        // carries a fresh generation number, and a CPU acknowledges by storing
        // that same number, so an acknowledgement can only ever refer to the
        // request whose mask it observed. `active_roots` records the root each
        // CPU installed most recently, which is what confines a user-half
        // shootdown: loading CR3 flushes every non-global entry, so only a CPU
        // currently running the root can still hold its user translations.
        alignas(cache_line_bytes) astia::atomic<u64> active_roots[max_cpus]{};
        alignas(cache_line_bytes) astia::atomic<u64> tlb_ack_seq[max_cpus]{};
        astia::atomic<u64>     tlb_request_seq{0};
        astia::atomic<vaddr_t> tlb_request_virt{0};
        astia::atomic<u64>     tlb_request_targets[tlb_mask_words]{};
        core::spinlock         tlb_lock;
        bool                   root_tracking_ready = false;

        // Acknowledge wait: poll for this many relax() calls, then re-ring the
        // doorbell in case the first IPI was swallowed by a busy LAPIC. Only a
        // target that has genuinely stopped executing trips the panic; there is
        // deliberately no "assume it arrived and continue" path.
        constexpr u32 tlb_wait_chunk  = 1u << 18;
        constexpr u32 tlb_wait_chunks = 1u << 10;

        bool tlb_targeted(u32 cpu) {
            return (tlb_request_targets[cpu / 64].load(astia::memory_order::relaxed) &
                    (u64{1} << (cpu % 64))) != 0;
        }

        bool tlb_acknowledged(u32 cpu, u64 seq) {
            return tlb_ack_seq[cpu].load(astia::memory_order::acquire) >= seq;
        }

        void tlb_ring(u32 cpu, u64 seq) {
            if (!tlb_acknowledged(cpu, seq))
                (void) lapic::send_ipi(logical_apic_ids[cpu], lapic::tlb_vector);
        }

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

            logical_apic_ids[static_cast<u32>(cpu_index)] = lapic::id();

            // The AP is running the kernel root loaded by the trampoline.
            // Record it before the CPU is counted online: once online it can
            // be picked for the scheduler and start running user code, and a
            // shootdown from then on must be able to reach it.
            if (static_cast<u32>(cpu_index) < max_cpus &&
                astia::arch::kernel_page_map() != nullptr)
                active_roots[cpu_index].store(astia::arch::kernel_page_map()->root(),
                                              astia::memory_order::release);

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
        cpu_total           = 1;
        cpu_online          = 1;
        scheduler_online    = 0;
        logical_apic_ids[0] = lapic::id();

        // Per-CPU GS and the LAPIC exist by now, so the BSP can be recorded
        // (and reached) from this point on. Early paging ran before GS was
        // installed, which is why note_active_root() stays a no-op until this
        // flag is published.
        if (astia::arch::kernel_page_map() != nullptr)
            active_roots[0].store(astia::arch::kernel_page_map()->root(),
                                  astia::memory_order::release);

        astia::atomic_ref<bool>(root_tracking_ready)
            .store(true, astia::memory_order::release);

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
            astia::acpi::detail::madt_error madt_error =
                astia::acpi::detail::madt_error::none;

            astia::acpi::detail::physical_reader context{};

            if (astia::acpi::detail::kernel_physical_reader(
                    info.memory, pci_identity_window_size, context)) {
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
                // An AP can trigger a kernel-half shootdown while it brings
                // itself up (heap growth), and this CPU is its target: keep
                // servicing requests instead of spinning with them pending.
                astia::arch::cpu::relax();
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

    void note_active_root(paddr_t root) {
        if (!astia::atomic_ref<bool>(root_tracking_ready)
                 .load(astia::memory_order::acquire))
            return;

        const u32 index = astia::arch::cpu::current_index();
        if (index < max_cpus)
            active_roots[index].store(root, astia::memory_order::release);
    }

    void drain_tlb_shootdown() {
        const u64 seq = tlb_request_seq.load(astia::memory_order::acquire);
        if (seq == 0)
            return;

        const u32 cpu = astia::arch::cpu::current_index();

        if (cpu >= max_cpus || !tlb_targeted(cpu))
            return;

        if (tlb_ack_seq[cpu].load(astia::memory_order::acquire) >= seq)
            return;

        // The initiator holds tlb_lock from publication until every ack is
        // in, so the request fields below cannot change underneath us.
        const vaddr_t virt = tlb_request_virt.load(astia::memory_order::relaxed);

        page_map::invalidate(virt);
        tlb_ack_seq[cpu].store(seq, astia::memory_order::release);
    }

    void shootdown_tlb(paddr_t root, vaddr_t virt, bool kernel_half) {
        if (!astia::atomic_ref<bool>(root_tracking_ready)
                 .load(astia::memory_order::acquire))
            return;

        const u32 self = astia::arch::cpu::current_index();
        if (self >= max_cpus)
            return;

        // `cpu_online` counts the dense [0, online) index range; an AP that
        // has not incremented it yet is not targetable and holds no user
        // translations, so it cannot have cached a user-half page.
        const u32 online =
            astia::atomic_ref<u32>(cpu_online).load(astia::memory_order::acquire);

        u64 targets[tlb_mask_words]{};
        u32 target_count = 0;

        for (u32 cpu = 0; cpu < online && cpu < max_cpus; ++cpu) {
            if (cpu == self)
                continue;

            if (!kernel_half &&
                active_roots[cpu].load(astia::memory_order::acquire) != root)
                continue;

            targets[cpu / 64] |= u64{1} << (cpu % 64);

            ++target_count;
        }

        if (target_count == 0)
            return;

        core::spinlock_guard guard(tlb_lock);

        const u64 seq = tlb_request_seq.load(astia::memory_order::relaxed) + 1;

        tlb_request_virt.store(virt, astia::memory_order::relaxed);

        for (usz word = 0; word < tlb_mask_words; ++word) {
            tlb_request_targets[word].store(targets[word], astia::memory_order::relaxed);
        }

        // The page-table store, the request fields and the generation all have
        // to be visible before the doorbells ring, or a target could
        // invalidate and refill from the old entry.
        astia::atomic_thread_fence(astia::memory_order::seq_cst);
        tlb_request_seq.store(seq, astia::memory_order::release);
        astia::atomic_thread_fence(astia::memory_order::seq_cst);

        for (u32 cpu = 0; cpu < online && cpu < max_cpus; ++cpu) {
            if ((targets[cpu / 64] & (u64{1} << (cpu % 64))) != 0)
                tlb_ring(cpu, seq);
        }

        for (u32 round = 0;; ++round) {
            u32 missing = 0;

            for (u32 cpu = 0; cpu < online && cpu < max_cpus; ++cpu) {
                if ((targets[cpu / 64] & (u64{1} << (cpu % 64))) == 0)
                    continue;

                if (tlb_acknowledged(cpu, seq))
                    continue;

                ++missing;

                if (round == tlb_wait_chunks)
                    core::kpanic("TLB shootdown: CPU %u never acknowledged seq=%llu", cpu,
                                 static_cast<unsigned long long>(seq));
            }

            if (missing == 0)
                return;

            // A target spinning with interrupts masked acknowledges from
            // inside relax(), so the initiator cannot deadlock against a lock
            // it is holding.
            for (u32 spin = 0; spin < tlb_wait_chunk; ++spin) {
                astia::arch::cpu::relax();
            }

            for (u32 cpu = 0; cpu < online && cpu < max_cpus; ++cpu) {
                if ((targets[cpu / 64] & (u64{1} << (cpu % 64))) != 0)
                    tlb_ring(cpu, seq);
            }
        }
    }
} // namespace astia::arch::x86_64::smp
