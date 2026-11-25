#include "gic.hpp"

namespace astia::arch::aarch64::gicv3 {
    namespace {
        constexpr u64 gicd_ctlr_offset       = 0x0000;
        constexpr u64 gicr_typer_offset      = 0x0008;
        constexpr u64 gicr_waker_offset      = 0x0014;
        constexpr u64 gicr_igroupr0_offset   = 0x0080;
        constexpr u64 gicr_isenabler0_offset = 0x0100;
        constexpr u64 gicr_ipriorityr_offset = 0x0400;
        constexpr u64 gicr_icfgr1_offset     = 0x0c04;
        constexpr u32 priority_registers     = 8;
        constexpr u32 wait_spin_limit        = 1000000;

        volatile u32 handled_sgi_count   = 0;
        volatile u32 handled_timer_count = 0;

        [[nodiscard]] u32 read32(u64 base, u64 offset) {
            return *reinterpret_cast<volatile const u32*>(base + offset);
        }

        void write32(u64 base, u64 offset, u32 value) {
            *reinterpret_cast<volatile u32*>(base + offset) = value;
        }

        [[nodiscard]] u64 read_mpidr_el1() {
            u64 value = 0;
            __asm__ volatile("mrs %0, mpidr_el1" : "=r"(value));
            return value;
        }

        void dsb_sy() {
            __asm__ volatile("dsb sy" : : : "memory");
        }

        void isb() {
            __asm__ volatile("isb" : : : "memory");
        }

        [[nodiscard]] u64 read_icc_sre_el1() {
            u64 value = 0;
            __asm__ volatile("mrs %0, icc_sre_el1" : "=r"(value));
            return value;
        }

        void write_icc_sre_el1(u64 value) {
            __asm__ volatile("msr icc_sre_el1, %0" : : "r"(value) : "memory");
        }

        [[nodiscard]] u64 read_icc_pmr_el1() {
            u64 value = 0;
            __asm__ volatile("mrs %0, icc_pmr_el1" : "=r"(value));
            return value;
        }

        void write_icc_pmr_el1(u64 value) {
            __asm__ volatile("msr icc_pmr_el1, %0" : : "r"(value) : "memory");
        }

        [[nodiscard]] u64 read_icc_bpr1_el1() {
            u64 value = 0;
            __asm__ volatile("mrs %0, icc_bpr1_el1" : "=r"(value));
            return value;
        }

        void write_icc_bpr1_el1(u64 value) {
            __asm__ volatile("msr icc_bpr1_el1, %0" : : "r"(value) : "memory");
        }

        [[nodiscard]] u64 read_icc_ctlr_el1() {
            u64 value = 0;
            __asm__ volatile("mrs %0, icc_ctlr_el1" : "=r"(value));
            return value;
        }

        void write_icc_ctlr_el1(u64 value) {
            __asm__ volatile("msr icc_ctlr_el1, %0" : : "r"(value) : "memory");
        }

        [[nodiscard]] u64 read_icc_igrpen1_el1() {
            u64 value = 0;
            __asm__ volatile("mrs %0, icc_igrpen1_el1" : "=r"(value));
            return value;
        }

        void write_icc_igrpen1_el1(u64 value) {
            __asm__ volatile("msr icc_igrpen1_el1, %0" : : "r"(value) : "memory");
        }

        [[nodiscard]] u64 read_icc_iar1_el1() {
            u64 value = 0;
            __asm__ volatile("mrs %0, icc_iar1_el1" : "=r"(value));
            return value;
        }

        void write_icc_eoir1_el1(u64 value) {
            __asm__ volatile("msr icc_eoir1_el1, %0" : : "r"(value) : "memory");
        }

        void write_icc_dir_el1(u64 value) {
            __asm__ volatile("msr icc_dir_el1, %0" : : "r"(value) : "memory");
        }

        void write_icc_sgi1r_el1(u64 value) {
            __asm__ volatile("msr icc_sgi1r_el1, %0" : : "r"(value) : "memory");
        }

        [[nodiscard]] bool wait_for_rwp(u64 distributor) {
            for (u32 spin = 0; spin < wait_spin_limit; ++spin) {
                if ((read32(distributor, gicd_ctlr_offset) & gicd_ctlr_rwp) == 0) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool wait_for_children_asleep(u64 redistributor) {
            for (u32 spin = 0; spin < wait_spin_limit; ++spin) {
                if ((read32(redistributor, gicr_waker_offset) &
                     gicr_waker_children_asleep) == 0) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] u64 find_redistributor(const region& redistributor, u32 affinity) {
            for (u64 offset = 0;
                 offset + minimum_redistributor_size <= redistributor.size;
                 offset += redistributor_stride) {
                const u64 frame      = redistributor.base + offset;
                const u32 typer_low  = read32(frame, gicr_typer_offset);
                const u32 typer_high = read32(frame, gicr_typer_offset + 4);
                if (typer_high == affinity) {
                    return frame;
                }
                if ((typer_low & gicr_typer_last) != 0) {
                    break;
                }
            }
            return 0;
        }

        void configure_redistributor(u64 redistributor) {
            const u64 sgi = redistributor + sgi_base_offset;

            write32(sgi, gicr_igroupr0_offset, gicr_igroupr0_group1);
            for (u32 index = 0; index < priority_registers; ++index) {
                write32(sgi, gicr_ipriorityr_offset + index * 4, gicr_priority_word);
            }
            write32(sgi, gicr_icfgr1_offset, 0);
            write32(sgi, gicr_isenabler0_offset,
                    (1u << self_sgi_id) | (1u << timer::ppi_id));
            dsb_sy();
        }
    } // namespace

    const char* describe(error value) {
        switch (value) {
        case error::none:
            return "none";
        case error::bad_region:
            return "bad-region";
        case error::not_single_security_state:
            return "not-single-security-state";
        case error::distributor_rwp_timeout:
            return "distributor-rwp-timeout";
        case error::distributor_groups_not_disabled:
            return "distributor-groups-not-disabled";
        case error::are_not_enabled:
            return "are-not-enabled";
        case error::distributor_group1_not_enabled:
            return "distributor-group1-not-enabled";
        case error::no_redistributor:
            return "no-redistributor";
        case error::redistributor_wake_timeout:
            return "redistributor-wake-timeout";
        case error::sgi_not_enabled:
            return "sgi-not-enabled";
        case error::timer_ppi_not_enabled:
            return "timer-ppi-not-enabled";
        case error::sre_not_enabled:
            return "sre-not-enabled";
        case error::sre_bypass_not_disabled:
            return "sre-bypass-not-disabled";
        case error::eoimode_not_set:
            return "eoimode-not-set";
        case error::group1_not_enabled:
            return "group1-not-enabled";
        }
        return "unknown";
    }

    error initialize(const boot_config& config, init_result& result) {
        if (!valid_region(config.distributor) || !valid_region(config.redistributor) ||
            config.distributor.size < minimum_distributor_size ||
            config.redistributor.size < minimum_redistributor_size) {
            return error::bad_region;
        }

        u32 ctlr = read32(config.distributor.base, gicd_ctlr_offset);
        if ((ctlr & gicd_ctlr_ds) == 0) {
            return error::not_single_security_state;
        }

        ctlr &= ~gicd_ctlr_group_enables;
        write32(config.distributor.base, gicd_ctlr_offset, ctlr);
        dsb_sy();
        if (!wait_for_rwp(config.distributor.base)) {
            return error::distributor_rwp_timeout;
        }
        ctlr = read32(config.distributor.base, gicd_ctlr_offset);
        if ((ctlr & gicd_ctlr_group_enables) != 0) {
            return error::distributor_groups_not_disabled;
        }

        ctlr |= gicd_ctlr_are_ns;
        write32(config.distributor.base, gicd_ctlr_offset, ctlr);
        dsb_sy();
        if (!wait_for_rwp(config.distributor.base)) {
            return error::distributor_rwp_timeout;
        }
        ctlr = read32(config.distributor.base, gicd_ctlr_offset);
        if ((ctlr & gicd_ctlr_are_ns) == 0) {
            return error::are_not_enabled;
        }

        ctlr |= gicd_ctlr_enable_grp1ns;
        write32(config.distributor.base, gicd_ctlr_offset, ctlr);
        dsb_sy();
        if (!wait_for_rwp(config.distributor.base)) {
            return error::distributor_rwp_timeout;
        }
        ctlr = read32(config.distributor.base, gicd_ctlr_offset);
        if ((ctlr & gicd_ctlr_enable_grp1ns) == 0) {
            return error::distributor_group1_not_enabled;
        }

        const u32 affinity      = affinity_from_mpidr(read_mpidr_el1());
        const u64 redistributor = find_redistributor(config.redistributor, affinity);
        if (redistributor == 0) {
            return error::no_redistributor;
        }

        u32 waker = read32(redistributor, gicr_waker_offset);
        waker &= ~gicr_waker_processor_sleep;
        write32(redistributor, gicr_waker_offset, waker);
        dsb_sy();
        if (!wait_for_children_asleep(redistributor)) {
            return error::redistributor_wake_timeout;
        }
        isb();

        timer::disable_and_clear();
        configure_redistributor(redistributor);
        const u32 enabled =
            read32(redistributor + sgi_base_offset, gicr_isenabler0_offset);
        if ((enabled & (1u << self_sgi_id)) == 0) {
            return error::sgi_not_enabled;
        }
        if ((enabled & (1u << timer::ppi_id)) == 0) {
            return error::timer_ppi_not_enabled;
        }

        u64 sre = read_icc_sre_el1();
        sre |= icc_sre_sre | icc_sre_bypass_disabled;
        write_icc_sre_el1(sre);
        isb();
        sre = read_icc_sre_el1();
        if ((sre & icc_sre_sre) == 0) {
            return error::sre_not_enabled;
        }
        if ((sre & icc_sre_bypass_disabled) != icc_sre_bypass_disabled) {
            return error::sre_bypass_not_disabled;
        }

        write_icc_pmr_el1(pmr_allow_priorities);
        isb();
        write_icc_bpr1_el1(bpr1_no_grouping);
        isb();

        u64 ctlr_el1 = read_icc_ctlr_el1();
        ctlr_el1 |= icc_ctlr_eoimode;
        write_icc_ctlr_el1(ctlr_el1);
        isb();
        ctlr_el1 = read_icc_ctlr_el1();
        if ((ctlr_el1 & icc_ctlr_eoimode) == 0) {
            return error::eoimode_not_set;
        }

        write_icc_igrpen1_el1(icc_igrpen1_enable);
        isb();
        dsb_sy();
        isb();
        const u64 igrpen1 = read_icc_igrpen1_el1();
        if ((igrpen1 & icc_igrpen1_enable) == 0) {
            return error::group1_not_enabled;
        }

        result.redistributor_base = redistributor;
        result.affinity           = affinity;
        result.distributor_ctlr   = ctlr;
        result.sre                = static_cast<u32>(sre);
        result.pmr                = static_cast<u32>(read_icc_pmr_el1());
        result.bpr1               = static_cast<u32>(read_icc_bpr1_el1());
        result.ctlr_el1           = static_cast<u32>(ctlr_el1);
        result.igrpen1            = static_cast<u32>(igrpen1);
        return error::none;
    }

    void trigger_self_sgi() {
        const u32 affinity = affinity_from_mpidr(read_mpidr_el1());
        write_icc_sgi1r_el1(self_sgi1r_value(affinity, self_sgi_id));
        isb();
        dsb_sy();
        isb();
    }

    irq_result acknowledge_and_complete() {
        const u32        iar      = static_cast<u32>(read_icc_iar1_el1());
        const u32        intid    = iar & intid_mask;
        const irq_action action   = classify_intid(intid);
        const bool       is_timer = intid == timer::ppi_id;
        if (action == irq_action::handled) {
            if (is_timer) {
                timer::disable_and_clear();
            }
            write_icc_eoir1_el1(iar);
            write_icc_dir_el1(intid);
            isb();
            dsb_sy();
            isb();
            if (is_timer) {
                handled_timer_count = handled_timer_count + 1;
            }
            else {
                handled_sgi_count = handled_sgi_count + 1;
            }
        }
        return irq_result{action, intid, iar, is_timer};
    }

    u32 self_sgi_count() {
        return handled_sgi_count;
    }

    u32 timer_count() {
        return handled_timer_count;
    }

} // namespace astia::arch::aarch64::gicv3
