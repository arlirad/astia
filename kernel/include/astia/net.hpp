#pragma once

#include <astia/api/errno.h>
#include <astia/common.hpp>
#include <astia/module_abi_net.h>

namespace astia::net {

    using packet_segment = astia_module_net_packet_segment;
    using packet_view = astia_module_net_packet_view;
    using tx_request = astia_module_net_tx_request;
    using device_abi = astia_module_net_device;
    using tx_reclaim_fn = astia_module_net_tx_reclaim_fn;
    using rx_irq_callback = astia_module_net_rx_irq_fn;
    using rx_deferred_callback = astia_module_net_rx_deferred_fn;

    enum class rx_action : u8 {
        recycle = ASTIA_MODULE_NET_RX_RECYCLE,
        defer = ASTIA_MODULE_NET_RX_DEFER,
        drop = ASTIA_MODULE_NET_RX_DROP,
    };

    enum link_flags : u32 {
        link_up = 1u << 0,
        link_running = 1u << 1,
        link_full_duplex = 1u << 2,
    };

    enum class device_state : u8 {
        detached,
        discovered,
        running,
        stopping,
        reset,
    };

    inline rx_action rx_result(u8 value) {
        return value == ASTIA_MODULE_NET_RX_DEFER
                   ? rx_action::defer
                   : value == ASTIA_MODULE_NET_RX_DROP ? rx_action::drop : rx_action::recycle;
    }

    inline void recycle(packet_view& packet, int status = 0) {
        if (packet.recycle != nullptr) {
            auto callback = packet.recycle;
            packet.recycle = nullptr;
            callback(packet.owner, status);
        }
    }

    template <typename T, u32 Capacity>
    class bounded_spsc_queue final {
        static_assert(Capacity >= 2 && (Capacity & (Capacity - 1u)) == 0);

        T values_[Capacity]{};
        volatile u32 head_ = 0;
        volatile u32 tail_ = 0;

        public:
        bounded_spsc_queue() = default;
        bounded_spsc_queue(const bounded_spsc_queue&) = delete;
        bounded_spsc_queue& operator=(const bounded_spsc_queue&) = delete;

        bool push(const T& value) {
            const u32 head = __atomic_load_n(&head_, __ATOMIC_RELAXED);
            const u32 tail = __atomic_load_n(&tail_, __ATOMIC_ACQUIRE);
            if (head - tail == Capacity)
                return false;
            values_[head & (Capacity - 1u)] = value;
            __atomic_store_n(&head_, head + 1u, __ATOMIC_RELEASE);
            return true;
        }

        bool pop(T& value) {
            const u32 tail = __atomic_load_n(&tail_, __ATOMIC_RELAXED);
            const u32 head = __atomic_load_n(&head_, __ATOMIC_ACQUIRE);
            if (tail == head)
                return false;
            value = values_[tail & (Capacity - 1u)];
            __atomic_store_n(&tail_, tail + 1u, __ATOMIC_RELEASE);
            return true;
        }

        u32 size() const {
            return __atomic_load_n(&head_, __ATOMIC_ACQUIRE) -
                   __atomic_load_n(&tail_, __ATOMIC_ACQUIRE);
        }
    };

    template <typename T, u32 Capacity>
    class bounded_mpsc_queue final {
        static_assert(Capacity >= 2 && (Capacity & (Capacity - 1u)) == 0);

        struct cell {
            volatile u32 sequence = 0;
            T value{};
        };

        cell cells_[Capacity]{};
        volatile u32 enqueue_position_ = 0;
        volatile u32 dequeue_position_ = 0;

        public:
        bounded_mpsc_queue() {
            for (u32 i = 0; i < Capacity; ++i)
                __atomic_store_n(&cells_[i].sequence, i, __ATOMIC_RELAXED);
        }
        bounded_mpsc_queue(const bounded_mpsc_queue&) = delete;
        bounded_mpsc_queue& operator=(const bounded_mpsc_queue&) = delete;

        bool push(const T& value) {
            u32 position = __atomic_load_n(&enqueue_position_, __ATOMIC_RELAXED);
            for (;;) {
                cell& slot = cells_[position & (Capacity - 1u)];
                const u32 sequence = __atomic_load_n(&slot.sequence, __ATOMIC_ACQUIRE);
                const i32 difference = static_cast<i32>(sequence - position);
                if (difference == 0) {
                    if (__atomic_compare_exchange_n(&enqueue_position_, &position, position + 1u,
                                                    false, __ATOMIC_RELAXED,
                                                    __ATOMIC_RELAXED)) {
                        slot.value = value;
                        __atomic_store_n(&slot.sequence, position + 1u, __ATOMIC_RELEASE);
                        return true;
                    }
                } else if (difference < 0) {
                    return false;
                } else {
                    position = __atomic_load_n(&enqueue_position_, __ATOMIC_RELAXED);
                }
            }
        }

        bool pop(T& value) {
            const u32 position = __atomic_load_n(&dequeue_position_, __ATOMIC_RELAXED);
            cell& slot = cells_[position & (Capacity - 1u)];
            const u32 sequence = __atomic_load_n(&slot.sequence, __ATOMIC_ACQUIRE);
            if (static_cast<i32>(sequence - (position + 1u)) < 0)
                return false;
            value = slot.value;
            __atomic_store_n(&slot.sequence, position + Capacity, __ATOMIC_RELEASE);
            __atomic_store_n(&dequeue_position_, position + 1u, __ATOMIC_RELAXED);
            return true;
        }
    };

    template <typename T, u32 Capacity>
    using bounded_lock_free_spsc = bounded_spsc_queue<T, Capacity>;

    template <typename T, u32 Capacity>
    using bounded_lock_free_mpsc = bounded_mpsc_queue<T, Capacity>;

    template <u32 PendingDepth = 16, u32 CompletionDepth = 4>
    class tx_path final {
        struct completion {
            tx_request request{};
            int status = 0;
        };

        bounded_mpsc_queue<tx_request, PendingDepth> pending_;
        bounded_spsc_queue<completion, CompletionDepth> completed_;
        u64 generation_ = 1;
        bool accepting_ = true;

        static void reclaim(tx_request& request, int status) {
            if (request.reclaim != nullptr) {
                const auto callback = request.reclaim;
                request.reclaim = nullptr;
                callback(request.cookie, &request, status);
            }
        }

        public:
        int submit(tx_request request) {
            if (!accepting_ || request.data == nullptr || request.length == 0 ||
                request.reclaim == nullptr)
                return -EINVAL;
            request.generation = generation_;
            return pending_.push(request) ? 0 : -EAGAIN;
        }

        bool dequeue(tx_request& request) { return pending_.pop(request); }

        bool complete(const tx_request& request, int status) {
            if (!accepting_ || request.generation != generation_)
                return false;
            return completed_.push({request, status});
        }

        bool cancel(const tx_request& request) {
            if (request.generation != generation_)
                return false;
            tx_request pending{};
            bool found = false;
            bounded_spsc_queue<tx_request, PendingDepth> retained;
            while (pending_.pop(pending)) {
                if (!found && pending.cookie == request.cookie) {
                    reclaim(pending, -ECANCELED);
                    found = true;
                } else {
                    retained.push(pending);
                }
            }
            while (retained.pop(pending))
                pending_.push(pending);
            return found;
        }

        u32 poll() {
            u32 count = 0;
            completion item{};
            while (completed_.pop(item)) {
                reclaim(item.request, item.status);
                ++count;
            }
            return count;
        }

        void reset() {
            accepting_ = false;
            tx_request request{};
            while (pending_.pop(request))
                reclaim(request, -ECANCELED);
            completion item{};
            while (completed_.pop(item))
                reclaim(item.request, -ECANCELED);
            ++generation_;
            if (generation_ == 0)
                generation_ = 1;
            accepting_ = true;
        }

        u64 generation() const { return generation_; }
    };

    class rx_path final {
        public:
        static constexpr u32 depth = 2;

        rx_path(rx_irq_callback irq, rx_deferred_callback deferred, void* context)
            : irq_(irq), deferred_(deferred), context_(context) {}

        rx_action dispatch_irq(packet_view packet) {
            const rx_action action = irq_ == nullptr ? rx_action::defer
                                                       : rx_result(irq_(context_, &packet));
            if (action == rx_action::defer && deferred_path_.push(packet))
                return action;
            recycle(packet, action == rx_action::defer ? -EAGAIN : 0);
            return action == rx_action::defer ? rx_action::drop : action;
        }

        u32 poll() {
            if (deferred_ == nullptr)
                return 0;
            u32 count = 0;
            packet_view packet{};
            while (deferred_path_.pop(packet)) {
                deferred_(context_, &packet);
                recycle(packet);
                ++count;
            }
            return count;
        }

        bool deferred_full() const { return deferred_path_.size() == depth; }

        private:
        rx_irq_callback irq_ = nullptr;
        rx_deferred_callback deferred_ = nullptr;
        void* context_ = nullptr;
        bounded_spsc_queue<packet_view, depth> deferred_path_;
    };

    class registry final {
        public:
        static constexpr u32 max_devices = 32;

        bool register_device(device_abi& device) {
            if (device.name == nullptr || device.name[0] == '\0' || device.context == nullptr)
                return false;
            for (u32 i = 0; i < count_; ++i)
                if (devices_[i] == &device)
                    return true;
            if (count_ == max_devices)
                return false;
            devices_[count_++] = &device;
            return true;
        }

        bool unregister_device(device_abi& device) {
            for (u32 i = 0; i < count_; ++i) {
                if (devices_[i] != &device)
                    continue;
                devices_[i] = devices_[--count_];
                devices_[count_] = nullptr;
                return true;
            }
            return false;
        }

        device_abi* at(u32 index) const { return index < count_ ? devices_[index] : nullptr; }
        u32 count() const { return count_; }

        private:
        device_abi* devices_[max_devices]{};
        u32 count_ = 0;
    };

    registry& devices();

} // namespace astia::net
