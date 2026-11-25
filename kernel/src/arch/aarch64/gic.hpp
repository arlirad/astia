#pragma once

#include <astia/common.hpp>

#include "timer.hpp"

namespace astia::arch::aarch64::gicv3 {

    inline constexpr u32 self_sgi_id       = 15;
    inline constexpr u32 max_sgi_id        = 15;
    inline constexpr u32 spurious_intid    = 1023;
    inline constexpr u32 special_intid_min = 1020;
    inline constexpr u32 intid_mask        = 0x00ffffff;

    inline constexpr u64 redistributor_stride       = 0x00020000;
    inline constexpr u64 sgi_base_offset            = 0x00010000;
    inline constexpr u64 minimum_distributor_size   = 0x00010000;
    inline constexpr u64 minimum_redistributor_size = redistributor_stride;

    inline constexpr u32 gicd_ctlr_enable_grp0   = 1u << 0;
    inline constexpr u32 gicd_ctlr_enable_grp1ns = 1u << 1;
    inline constexpr u32 gicd_ctlr_are_ns        = 1u << 4;
    inline constexpr u32 gicd_ctlr_ds            = 1u << 6;
    inline constexpr u32 gicd_ctlr_group_enables =
        gicd_ctlr_enable_grp0 | gicd_ctlr_enable_grp1ns;
    inline constexpr u32 gicd_ctlr_rwp              = 1u << 31;
    inline constexpr u32 gicr_waker_processor_sleep = 1u << 1;
    inline constexpr u32 gicr_waker_children_asleep = 1u << 2;
    inline constexpr u32 gicr_typer_last            = 1u << 4;
    inline constexpr u32 gicr_igroupr0_group1       = 0xffffffffu;
    inline constexpr u32 gicr_priority_word         = 0xa0a0a0a0u;
    inline constexpr u32 gicr_priority              = 0xa0;
    inline constexpr u32 pmr_allow_priorities       = 0xf0;
    inline constexpr u32 bpr1_no_grouping           = 0;
    inline constexpr u32 icc_ctlr_eoimode           = 1u << 1;
    inline constexpr u32 icc_sre_sre                = 1u << 0;
    inline constexpr u32 icc_sre_dfb                = 1u << 1;
    inline constexpr u32 icc_sre_dib                = 1u << 2;
    inline constexpr u32 icc_sre_bypass_disabled    = icc_sre_dfb | icc_sre_dib;
    inline constexpr u32 icc_igrpen1_enable         = 1u << 0;

    enum class error : u8 {
        none,
        bad_region,
        not_single_security_state,
        distributor_rwp_timeout,
        distributor_groups_not_disabled,
        are_not_enabled,
        distributor_group1_not_enabled,
        no_redistributor,
        redistributor_wake_timeout,
        sgi_not_enabled,
        timer_ppi_not_enabled,
        sre_not_enabled,
        sre_bypass_not_disabled,
        eoimode_not_set,
        group1_not_enabled,
    };

    const char* describe(error value);

    struct region {
        u64 base = 0;
        u64 size = 0;
    };

    struct boot_config {
        region distributor;
        region redistributor;
    };

    struct init_result {
        u64 redistributor_base = 0;
        u32 affinity           = 0;
        u32 distributor_ctlr   = 0;
        u32 sre                = 0;
        u32 pmr                = 0;
        u32 bpr1               = 0;
        u32 ctlr_el1           = 0;
        u32 igrpen1            = 0;
    };

    enum class irq_action : u8 {
        spurious,
        handled,
        fatal,
    };

    struct irq_result {
        irq_action action = irq_action::fatal;
        u32        intid  = 0;
        u32        iar    = 0;
        bool       timer  = false;
    };

    [[nodiscard]] constexpr bool add_checked(u64 left, u64 right, u64& result) {
        if (left > ~u64{0} - right) {
            return false;
        }
        result = left + right;
        return true;
    }

    [[nodiscard]] constexpr bool valid_region(region value) {
        u64 end = 0;
        return value.size != 0 && (value.base & 0xfffULL) == 0 &&
               (value.size & 0xfffULL) == 0 && add_checked(value.base, value.size, end);
    }

    [[nodiscard]] constexpr u32 affinity_from_mpidr(u64 mpidr) {
        return static_cast<u32>(mpidr & 0x00ffffffULL) |
               static_cast<u32>(((mpidr >> 32) & 0xffULL) << 24);
    }

    [[nodiscard]] constexpr u64 self_sgi1r_value(u32 affinity, u32 intid) {
        const u32 aff0        = affinity & 0xff;
        const u64 target_list = u64{1} << (aff0 & 0x0f);
        const u64 rs          = u64{(aff0 >> 4) & 0x0f} << 44;
        return target_list | (u64{(affinity >> 8) & 0xff} << 16) |
               (u64{intid & 0x0f} << 24) | (u64{(affinity >> 16) & 0xff} << 32) |
               (u64{(affinity >> 24) & 0xff} << 48) | rs;
    }

    [[nodiscard]] constexpr irq_action classify_intid(u32 intid) {
        if (intid >= special_intid_min) {
            return intid == spurious_intid ? irq_action::spurious : irq_action::fatal;
        }
        if (intid <= max_sgi_id && intid == self_sgi_id) {
            return irq_action::handled;
        }
        if (intid == timer::ppi_id) {
            return irq_action::handled;
        }
        return irq_action::fatal;
    }

    error      initialize(const boot_config& config, init_result& result);
    void       trigger_self_sgi();
    irq_result acknowledge_and_complete();
    u32        self_sgi_count();
    u32        timer_count();

} // namespace astia::arch::aarch64::gicv3
