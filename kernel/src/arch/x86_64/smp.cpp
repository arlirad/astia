#include "smp.hpp"

#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/arch/layout.hpp>
#include <astia/arch/page_map.hpp>
#include <astia/arch/platform.hpp>
#include <astia/log.hpp>
#include <astia/mm/heap.hpp>

#include "../../proc/scheduler.hpp"
#include "gdt.hpp"
#include "lapic.hpp"

extern "C" {
extern volatile astia::u32 astia_ap_cr3;
extern volatile astia::u64 astia_ap_stack, astia_ap_entry, astia_ap_cpu;
extern void                astia_ap_trampoline_start();
extern void                astia_ap_trampoline_end();
}

namespace astia::arch::x86_64::smp {
    namespace {
        using logs = astia::log::context<"smp">;

        constexpr u64 rsdp_signature = 0x2052545020445352ull; // "RSD PTR "
        constexpr u32 madt_signature = 0x43495041;            // "APIC"
        constexpr u32 xsdt_signature = 0x54445358;            // "XSDT"
        constexpr u32 rsdt_signature = 0x54445352;            // "RSDT"
        constexpr u32 max_cpus       = platform::max_cpu_count;

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

            __atomic_fetch_add(&cpu_online, 1, __ATOMIC_RELEASE);
            logs::info("AP%u online", static_cast<u32>(cpu_index));
            astia::arch::cpu::interrupts();
            logs::info("AP%u park", static_cast<u32>(cpu_index));

            while (__atomic_load_n(&scheduler_online, __ATOMIC_ACQUIRE) == 0) {
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

        const void* phys_ptr(u64 p) {
            // PVH's early identity window covers the low physical area where
            // firmware tables normally live.  The direct map covers the rest.
            return reinterpret_cast<const void*>(p < (1ull << 30) ? p : kernel_base + p);
        }

        u8 checksum(const void* p, usize n) {
            const auto* b = static_cast<const u8*>(p);

            u8 sum = 0;

            for (usize i = 0; i < n; ++i) {
                sum = static_cast<u8>(sum + b[i]);
            }

            return sum;
        }

        struct [[gnu::packed]] rsdp {
            char signature[8];
            u8   checksum;
            char oem[6];
            u8   revision;
            u32  rsdt;
            u32  length;
            u64  xsdt;
            u8   extended_checksum;
            u8   reserved[3];
        };

        struct [[gnu::packed]] sdt_header {
            u32 signature;
            u32 length;
            u8  revision;
            u8  checksum;
            u8  oem[6];
            u8  oem_table[8];
            u32 oem_revision;
            u32 creator;
            u32 creator_revision;
        };

        struct [[gnu::packed]] madt_header {
            sdt_header header;
            u32        lapic_address;
            u32        flags;
        };

        u32 count_madt(const madt_header* madt) {
            if (madt->header.length < sizeof(madt_header) ||
                checksum(madt, madt->header.length) != 0)
                return 0;

            u32 count = 0;

            const auto* p   = reinterpret_cast<const u8*>(madt) + sizeof(madt_header);
            const auto* end = reinterpret_cast<const u8*>(madt) + madt->header.length;

            while (p + 2 <= end) {
                const u8 type   = p[0];
                const u8 length = p[1];

                if (length < 2 || p + length > end)
                    break;

                u32 flags = 0;

                if (type == 0 && length >= 8)
                    flags = reinterpret_cast<const u32*>(p + 4)[0];
                else if (type == 9 && length >= 16)
                    flags = reinterpret_cast<const u32*>(p + 8)[0];

                if (((type == 0 && length >= 8) || (type == 9 && length >= 16)) &&
                    (flags & 1u)) {
                    if (count < max_cpus) {
                        const u32 id = type == 0
                                           ? p[3]
                                           : static_cast<u32>(
                                                 reinterpret_cast<const u32*>(p + 4)[0]);

                        for (u32 i = 0; i < count; ++i) {
                            if (apic_ids[i] == id)
                                return 0;
                        }

                        apic_ids[count++] = id;
                    }
                }

                p += length;
            }

            return count == 0 ? 1 : count;
        }

        u32 find_madt(const rsdp* r) {
            if (r->revision >= 2 && r->length >= sizeof(rsdp) && r->length <= 4096 &&
                r->xsdt != 0 && checksum(r, r->length) == 0) {
                const auto* table = static_cast<const sdt_header*>(phys_ptr(r->xsdt));

                if (table->signature == xsdt_signature &&
                    table->length >= sizeof(sdt_header) &&
                    checksum(table, table->length) == 0) {
                    const u32 n = (table->length - sizeof(sdt_header)) / 8;

                    const auto* entries = reinterpret_cast<const u64*>(
                        reinterpret_cast<const u8*>(table) + sizeof(sdt_header));

                    for (u32 i = 0; i < n; ++i) {
                        const auto* h =
                            static_cast<const sdt_header*>(phys_ptr(entries[i]));

                        if (h->signature == madt_signature)
                            return count_madt(reinterpret_cast<const madt_header*>(h));
                    }
                }
            }

            if (r->rsdt != 0 && checksum(r, 20) == 0) {
                const auto* table = static_cast<const sdt_header*>(phys_ptr(r->rsdt));

                if (table->signature == rsdt_signature &&
                    table->length >= sizeof(sdt_header) &&
                    checksum(table, table->length) == 0) {
                    const u32 n = (table->length - sizeof(sdt_header)) / 4;

                    const auto* entries = reinterpret_cast<const u32*>(
                        reinterpret_cast<const u8*>(table) + sizeof(sdt_header));

                    for (u32 i = 0; i < n; ++i) {
                        const auto* h =
                            static_cast<const sdt_header*>(phys_ptr(entries[i]));

                        if (h->signature == madt_signature)
                            return count_madt(reinterpret_cast<const madt_header*>(h));
                    }
                }
            }

            return 0;
        }
    } // namespace

    bool init(const boot::boot_info& info) {
        cpu_total        = 1;
        cpu_online       = 1;
        scheduler_online = 0;

        __atomic_store_n(&reschedule_pending[0], 0u, __ATOMIC_RELAXED);

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

        const auto* r =
            have_acpi ? static_cast<const rsdp*>(phys_ptr(info.rsdp)) : nullptr;

        if (have_acpi && (*reinterpret_cast<const u64*>(r->signature) != rsdp_signature ||
                          checksum(r, 20) != 0)) {
            have_acpi = false;
            cpu_total = platform::clamp_cpu_count(cpuid_threads());

            logs::info("discovered=%u online=1 (CPUID; invalid ACPI RSDP)", cpu_total);
            logs::info("reschedule IPI armed");

            if (cpu_total > 1) {
                logs::info(
                    "AP startup deferred: no APIC-ID topology (invalid ACPI RSDP)");

                return true;
            }
        }

        const u32 found = have_acpi ? find_madt(r) : 0;

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
                found == 0 && r != nullptr
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

            constexpr usize ap_stack_bytes = 16 * 1024;

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

            for (u32 wait = 0; wait < 1000000 &&
                               __atomic_load_n(&cpu_online, __ATOMIC_ACQUIRE) < index + 1;
                 ++wait) {
            }

            if (__atomic_load_n(&cpu_online, __ATOMIC_ACQUIRE) < index + 1) {
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
        const u32 online = __atomic_load_n(&cpu_online, __ATOMIC_ACQUIRE);

        for (u32 index = 1; index < online && index < max_cpus; ++index) {
            if (!astia::proc::scheduler::init_cpu(index)) {
                logs::error("AP%u scheduler initialization failed", index);

                return false;
            }
        }

        __atomic_store_n(&scheduler_online, 1u, __ATOMIC_RELEASE);

        if (__atomic_load_n(&cpu_online, __ATOMIC_ACQUIRE) > 1)
            request_reschedule();

        return true;
    }

    u32 online_cpus() {
        return cpu_online;
    }

    void request_reschedule() {
        request_local_reschedule();

        const u32 online = __atomic_load_n(&cpu_online, __ATOMIC_ACQUIRE);

        for (u32 index = 1; index < online && index < max_cpus; ++index) {
            __atomic_store_n(&reschedule_pending[index], 1u, __ATOMIC_RELEASE);
            (void) lapic::send_ipi(apic_ids[index], lapic::reschedule_vector);
        }
    }

    void request_local_reschedule() {
        const u32 index = astia::arch::cpu::current_index();
        if (index < max_cpus)
            __atomic_store_n(&reschedule_pending[index], 1u, __ATOMIC_RELEASE);
    }

    // Called by the vector-49 epilogue while the interrupt gate is still
    // closed.  Keep the interrupt handler itself to flagging and EOI only.
    void request_local_reschedule_from_interrupt() {
        request_local_reschedule();
    }

    bool consume_reschedule(u32 cpu_index) {
        if (cpu_index >= max_cpus)
            return false;

        return __atomic_exchange_n(&reschedule_pending[cpu_index], 0u,
                                   __ATOMIC_ACQ_REL) != 0;
    }
} // namespace astia::arch::x86_64::smp
