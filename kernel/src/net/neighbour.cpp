#include <astia/arch/cpu.hpp>
#include <astia/arch/timer.hpp>
#include <astia/core/completion.hpp>
#include <astia/core/timer.hpp>
#include <astia/mm/shptr.hpp>
#include <astia/net/neighbour.hpp>

#include "net_lock.hpp"

#include <new>

namespace astia::net {
    namespace {
        constexpr u32 max_entries     = 32;
        constexpr u32 max_pending     = 16;
        constexpr u32 max_discoverers = 8;

        struct neighbour_entry {
            link*        egress   = nullptr;
            protocol_id  protocol = protocol_id::none;
            u8           length   = 0;
            u8           address[protocol_address::max_length]{};
            link_address resolved{};
            bool         used = false;
        };

        neighbour_entry entries[max_entries]{};
        core::spinlock  entries_lock;

        /// Kernel-owned wait state: the completion lives here, not in the
        /// caller's coroutine frame, so a timer callback may complete it after
        /// the waiting task was destroyed.  `refs` counts the waiter and the
        /// armed timeout; the slot is reused only when both are gone.
        struct pending_entry {
            link*                          egress   = nullptr;
            protocol_id                    protocol = protocol_id::none;
            u8                             length   = 0;
            u8                             address[protocol_address::max_length]{};
            core::completion<link_address> done{};
            u8                             refs       = 0;
            bool                           registered = false;
        };

        pending_entry  pending[max_pending]{};
        core::spinlock pending_lock;

        struct discoverer_entry {
            mm::shptr<discoverer> value{};
        };

        discoverer_entry registrations[max_discoverers]{};
        core::spinlock   registrations_lock;

        bool same_address(protocol_id protocol, span<const u8> address,
                          protocol_id other_protocol, u8 other_length,
                          const u8* other_address) {
            if (protocol != other_protocol || address.size() != other_length)
                return false;

            for (u32 index = 0; index < other_length; ++index) {
                if (address[index] != other_address[index])
                    return false;
            }

            return true;
        }

        u64 retry_ticks() {
            const u32 hz = arch::timer_hz();
            if (hz == 0)
                return 0;

            return (static_cast<u64>(discover_retry_ms) * hz + 999) / 1000;
        }

        void reset_pending(pending_entry& entry) {
            entry.~pending_entry();
            new (static_cast<void*>(&entry)) pending_entry{};
        }

        void release_pending_ref(pending_entry& entry) {
            detail::lock_guard guard(pending_lock);

            if (entry.refs > 0)
                --entry.refs;

            if (entry.refs == 0)
                reset_pending(entry);
        }

        pending_entry* acquire_pending(link& egress, protocol_id protocol,
                                       span<const u8> address) {
            detail::lock_guard guard(pending_lock);

            for (pending_entry& entry : pending) {
                if (entry.refs != 0)
                    continue;

                reset_pending(entry);

                entry.egress     = &egress;
                entry.protocol   = protocol;
                entry.length     = static_cast<u8>(address.size());
                entry.refs       = 1; // waiter
                entry.registered = true;

                for (u32 index = 0; index < entry.length; ++index) {
                    entry.address[index] = address[index];
                }

                return &entry;
            }

            return nullptr;
        }

        void pending_timeout(void* context) {
            auto& entry = *static_cast<pending_entry*>(context);

            {
                detail::lock_guard guard(pending_lock);

                entry.registered = false;
            }

            entry.done.fail_deferred(error_t::etimedout);

            release_pending_ref(entry);
        }

        bool arm_pending_timeout(pending_entry& entry, u64 ticks) {
            {
                detail::lock_guard guard(pending_lock);

                ++entry.refs; // timeout
            }

            if (core::detail::arm_timer_callback(&entry, &pending_timeout, ticks))
                return true;

            release_pending_ref(entry);

            return false;
        }

        /// Disarms the timeout and drops the waiter reference; runs on both
        /// the resumed and the destroyed-coroutine path.
        struct pending_wait {
            pending_entry* entry = nullptr;

            ~pending_wait() {
                if (entry == nullptr)
                    return;

                if (core::detail::disarm_timer(entry))
                    release_pending_ref(*entry);

                release_pending_ref(*entry);

                entry = nullptr;
            }
        };

        mm::shptr<discoverer> acquire_discoverer(link& egress, protocol_id protocol) {
            for (u32 index = 0; index < max_discoverers; ++index) {
                mm::shptr<discoverer> candidate{};

                {
                    detail::lock_guard guard(registrations_lock);

                    candidate = registrations[index].value;
                }

                if (!candidate)
                    continue;

                if (candidate->supports(egress, protocol))
                    return candidate;
            }

            return {};
        }

        u32 take_pending(link& egress, protocol_id protocol, span<const u8> address,
                         pending_entry** out, u32 capacity) {
            detail::lock_guard guard(pending_lock);

            u32 count = 0;

            for (pending_entry& entry : pending) {
                if (!entry.registered || entry.egress != &egress)
                    continue;

                if (!same_address(protocol, address, entry.protocol, entry.length,
                                  entry.address))
                    continue;

                entry.registered = false;

                ++entry.refs; // held until the completion call returns

                if (count < capacity)
                    out[count++] = &entry;
            }

            return count;
        }
    } // namespace

    bool register_discoverer(discoverer& value) {
        detail::lock_guard guard(registrations_lock);

        for (discoverer_entry& entry : registrations) {
            if (entry.value && entry.value.get() == &value)
                return true;
        }

        for (discoverer_entry& entry : registrations) {
            if (entry.value)
                continue;

            entry.value = mm::shptr<discoverer>::borrowed(&value);

            return entry.value.get() == &value;
        }

        return false;
    }

    void unregister_discoverer(discoverer& value) {
        mm::shptr<discoverer> retiring{};

        {
            detail::lock_guard guard(registrations_lock);

            for (discoverer_entry& entry : registrations) {
                if (!entry.value || entry.value.get() != &value)
                    continue;

                retiring    = entry.value;
                entry.value = {};

                break;
            }
        }

        if (!retiring)
            return;

        mm::shweak<discoverer> watch = retiring.weak();

        retiring = {};

        while (!watch.expired()) {
            arch::cpu::relax();
        }
    }

    core::task<link_address> discover(link& egress, protocol_id protocol,
                                      span<const u8> address, core::cltoken token) {
        if (token.canceled())
            co_return error_t::ecanceled;

        if (protocol == protocol_id::none || address.empty() ||
            address.size() > protocol_address::max_length)
            co_return error_t::einval;

        {
            detail::lock_guard guard(entries_lock);

            for (neighbour_entry& entry : entries) {
                if (!entry.used || entry.egress != &egress)
                    continue;

                if (same_address(protocol, address, entry.protocol, entry.length,
                                 entry.address))
                    co_return entry.resolved;
            }
        }

        const u64 ticks = retry_ticks();

        for (u32 attempt = 0; attempt <= discover_max_retransmissions; ++attempt) {
            if (token.canceled())
                co_return error_t::ecanceled;

            mm::shptr<discoverer> value = acquire_discoverer(egress, protocol);
            if (!value)
                co_return error_t::enodev;

            pending_entry* entry = acquire_pending(egress, protocol, address);
            if (entry == nullptr)
                co_return error_t::eagain;

            pending_wait wait{entry};

            if (ticks == 0 || !arm_pending_timeout(*entry, ticks))
                co_return error_t::eagain;

            const error_t emitted =
                co_await value->discover(egress, protocol, address, token);

            if (emitted != error_t::enone)
                co_return emitted;

            const auto result = co_await entry->done;

            if (result.has_value)
                co_return result.value;

            if (result.error != error_t::etimedout)
                co_return result.error;
        }

        co_return error_t::etimedout;
    }

    void learn(link& egress, protocol_id protocol, span<const u8> address,
               const link_address& resolved) {
        if (protocol == protocol_id::none || address.empty() ||
            address.size() > protocol_address::max_length || !resolved.valid())
            return;

        {
            detail::lock_guard guard(entries_lock);

            neighbour_entry* free_entry = nullptr;

            for (neighbour_entry& entry : entries) {
                if (entry.used && entry.egress == &egress &&
                    same_address(protocol, address, entry.protocol, entry.length,
                                 entry.address)) {
                    entry.resolved = resolved;
                    free_entry     = &entry;

                    break;
                }

                if (!entry.used && free_entry == nullptr)
                    free_entry = &entry;
            }

            if (free_entry != nullptr && !free_entry->used) {
                free_entry->egress   = &egress;
                free_entry->protocol = protocol;
                free_entry->length   = static_cast<u8>(address.size());
                free_entry->resolved = resolved;
                free_entry->used     = true;

                for (u32 index = 0; index < free_entry->length; ++index) {
                    free_entry->address[index] = address[index];
                }
            }
        }

        pending_entry* waiters[max_pending]{};

        const u32 count = take_pending(egress, protocol, address, waiters, max_pending);

        for (u32 index = 0; index < count; ++index) {
            waiters[index]->done.complete_deferred(resolved);

            release_pending_ref(*waiters[index]);
        }
    }

    void forget(link& egress) {
        {
            detail::lock_guard guard(entries_lock);

            for (neighbour_entry& entry : entries) {
                if (entry.used && entry.egress == &egress)
                    entry = {};
            }
        }

        pending_entry* waiters[max_pending]{};

        u32 count = 0;

        {
            detail::lock_guard guard(pending_lock);

            for (pending_entry& entry : pending) {
                if (!entry.registered || entry.egress != &egress)
                    continue;

                entry.registered = false;

                ++entry.refs; // held until the completion call returns

                if (count < max_pending)
                    waiters[count++] = &entry;
            }
        }

        for (u32 index = 0; index < count; ++index) {
            waiters[index]->done.fail_deferred(error_t::enodev);

            release_pending_ref(*waiters[index]);
        }
    }
} // namespace astia::net
