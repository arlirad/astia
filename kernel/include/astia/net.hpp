#pragma once

#include <astia/api/errno.h>
#include <astia/common.hpp>
#include <astia/mm/shptr.hpp>

namespace astia::net {

    struct packet_segment {
        const u8* data = nullptr;
        u32 length = 0;
    };

    struct packet_view {
        packet_segment first{};
        packet_segment second{};
        u32 length = 0;
        u32 wire_length = 0;
        u64 generation = 0;
        void* owner = nullptr;
        void (*recycle)(void* owner, int status) = nullptr;
    };

    struct tx_request;
    using tx_reclaim_fn = void (*)(void*, const tx_request*, int);

    struct tx_request {
        const u8* data = nullptr;
        u64 physical = 0;
        u32 length = 0;
        u64 generation = 0;
        void* cookie = nullptr;
        tx_reclaim_fn reclaim = nullptr;
    };

    enum class rx_action : u8 { recycle, defer, drop };
    enum link_flags : u32 {
        link_up = 1u << 0,
        link_running = 1u << 1,
        link_full_duplex = 1u << 2,
    };

    using rx_irq_callback = u8 (*)(void*, const packet_view*);
    using rx_deferred_callback = void (*)(void*, const packet_view*);

    enum class device_state : u8 {
        detached,
        discovered,
        running,
        stopping,
        reset,
    };

    inline rx_action rx_result(u8 value) {
        return value == 1 ? rx_action::defer : value == 2 ? rx_action::drop : rx_action::recycle;
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
        bounded_spsc_queue() { initialize(); }
        bounded_spsc_queue(const bounded_spsc_queue&) = delete;
        bounded_spsc_queue& operator=(const bounded_spsc_queue&) = delete;

        void initialize() {
            __atomic_store_n(&head_, 0, __ATOMIC_RELAXED);
            __atomic_store_n(&tail_, 0, __ATOMIC_RELAXED);
        }

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
            initialize();
        }
        bounded_mpsc_queue(const bounded_mpsc_queue&) = delete;
        bounded_mpsc_queue& operator=(const bounded_mpsc_queue&) = delete;

        void initialize() {
            __atomic_store_n(&enqueue_position_, 0, __ATOMIC_RELAXED);
            __atomic_store_n(&dequeue_position_, 0, __ATOMIC_RELAXED);
            for (u32 i = 0; i < Capacity; ++i)
                __atomic_store_n(&cells_[i].sequence, i, __ATOMIC_RELAXED);
        }

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
        volatile u64 generation_ = 1;
        volatile u32 accepting_ = 1;
        volatile u32 submitters_ = 0;
        volatile u32 completers_ = 0;
        volatile u32 consumer_lock_ = 0;

        void lock_consumer() {
            u32 expected = 0;
            while (!__atomic_compare_exchange_n(&consumer_lock_, &expected, 1, false,
                                                 __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
                expected = 0;
                asm volatile("pause");
            }
        }

        void unlock_consumer() {
            __atomic_store_n(&consumer_lock_, 0, __ATOMIC_RELEASE);
        }

        void reclaim(tx_request& request, int status) {
            if (request.reclaim != nullptr) {
                const auto callback = request.reclaim;
                request.reclaim = nullptr;
                callback(context_, &request, status);
            }
        }

        void stop_accepting() {
            __atomic_store_n(&accepting_, 0, __ATOMIC_RELEASE);
            while (__atomic_load_n(&submitters_, __ATOMIC_ACQUIRE) != 0 ||
                   __atomic_load_n(&completers_, __ATOMIC_ACQUIRE) != 0)
                asm volatile("pause");
        }

        void start_accepting() {
            __atomic_store_n(&accepting_, 1, __ATOMIC_RELEASE);
        }

        public:
        explicit tx_path(void* context = nullptr) { initialize(context); }

        tx_path(const tx_path&) = delete;
        tx_path& operator=(const tx_path&) = delete;

        void initialize(void* context = nullptr) {
            pending_.initialize();
            completed_.initialize();
            __atomic_store_n(&generation_, 1, __ATOMIC_RELAXED);
            __atomic_store_n(&accepting_, 1, __ATOMIC_RELAXED);
            __atomic_store_n(&submitters_, 0, __ATOMIC_RELAXED);
            __atomic_store_n(&completers_, 0, __ATOMIC_RELAXED);
            __atomic_store_n(&consumer_lock_, 0, __ATOMIC_RELAXED);
            context_ = context;
        }

        int submit(tx_request request) {
            if (request.data == nullptr || request.length == 0 ||
                request.reclaim == nullptr)
                return -EINVAL;
            if (__atomic_load_n(&accepting_, __ATOMIC_ACQUIRE) == 0)
                return -ECANCELED;
            __atomic_add_fetch(&submitters_, 1, __ATOMIC_ACQUIRE);
            if (__atomic_load_n(&accepting_, __ATOMIC_ACQUIRE) == 0) {
                __atomic_sub_fetch(&submitters_, 1, __ATOMIC_RELEASE);
                return -ECANCELED;
            }
            request.generation = __atomic_load_n(&generation_, __ATOMIC_ACQUIRE);
            const bool pushed = pending_.push(request);
            __atomic_sub_fetch(&submitters_, 1, __ATOMIC_RELEASE);
            return pushed ? 0 : -EAGAIN;
        }

        bool dequeue(tx_request& request) {
            lock_consumer();
            const bool result = pending_.pop(request);
            unlock_consumer();
            return result;
        }

        bool complete(const tx_request& request, int status) {
            __atomic_add_fetch(&completers_, 1, __ATOMIC_ACQUIRE);
            if (__atomic_load_n(&accepting_, __ATOMIC_ACQUIRE) == 0 ||
                request.generation != __atomic_load_n(&generation_, __ATOMIC_ACQUIRE)) {
                __atomic_sub_fetch(&completers_, 1, __ATOMIC_RELEASE);
                return false;
            }
            const bool pushed = completed_.push({request, status});
            __atomic_sub_fetch(&completers_, 1, __ATOMIC_RELEASE);
            return pushed;
        }

        bool cancel(const tx_request& request) {
            const u64 generation = __atomic_load_n(&generation_, __ATOMIC_ACQUIRE);
            if (request.generation != 0 && request.generation != generation)
                return false;
            stop_accepting();
            lock_consumer();
            tx_request pending{};
            bool found = false;
            bounded_spsc_queue<tx_request, PendingDepth> retained;
            while (pending_.pop(pending)) {
                if (!found && pending.cookie == request.cookie &&
                    pending.generation == generation) {
                    reclaim(pending, -ECANCELED);
                    found = true;
                } else {
                    retained.push(pending);
                }
            }
            while (retained.pop(pending))
                pending_.push(pending);
            unlock_consumer();
            start_accepting();
            return found;
        }

        u32 poll() {
            lock_consumer();
            u32 count = 0;
            completion item{};
            while (completed_.pop(item)) {
                reclaim(item.request, item.status);
                ++count;
            }
            unlock_consumer();
            return count;
        }

        void reset() {
            stop_accepting();
            lock_consumer();
            tx_request request{};
            while (pending_.pop(request))
                reclaim(request, -ECANCELED);
            completion item{};
            while (completed_.pop(item))
                reclaim(item.request, -ECANCELED);
            u64 generation = __atomic_load_n(&generation_, __ATOMIC_RELAXED) + 1;
            if (generation == 0)
                generation = 1;
            __atomic_store_n(&generation_, generation, __ATOMIC_RELEASE);
            unlock_consumer();
            start_accepting();
        }

        bool abort_inflight(const tx_request& request, int status) {
            if (request.reclaim == nullptr)
                return false;
            tx_request copy = request;
            reclaim(copy, status);
            return true;
        }

        void set_context(void* context) { context_ = context; }
        bool accepting() const {
            return __atomic_load_n(&accepting_, __ATOMIC_ACQUIRE) != 0;
        }
        void set_generation(u64 value) {
            __atomic_store_n(&generation_, value == 0 ? 1 : value, __ATOMIC_RELEASE);
        }
        u64 generation() const { return __atomic_load_n(&generation_, __ATOMIC_ACQUIRE); }

        private:
        void* context_ = nullptr;
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

        rx_action invoke_irq(packet_view& packet) const {
            return irq_ == nullptr ? rx_action::defer : rx_result(irq_(context_, &packet));
        }

        void invoke_deferred(packet_view& packet) const {
            if (deferred_ != nullptr)
                deferred_(context_, &packet);
        }

        void set_callbacks(rx_irq_callback irq, rx_deferred_callback deferred, void* context) {
            irq_ = irq;
            deferred_ = deferred;
            context_ = context;
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

    class netdev final {
        public:
        static constexpr u32 tx_pending_depth = 16;
        static constexpr u32 tx_completion_depth = 4;

        netdev(const char* name, void* context = nullptr,
               rx_irq_callback irq = nullptr, rx_deferred_callback deferred = nullptr)
            : context_(context), rx_(irq, deferred, context), tx_(context) {
            initialize(name, context, irq, deferred);
        }

        void initialize(const char* name, void* context = nullptr,
                        rx_irq_callback irq = nullptr,
                        rx_deferred_callback deferred = nullptr) {
            name_[0] = '\0';
            u32 index = 0;
            while (index < sizeof(name_) - 1 && name != nullptr && name[index] != '\0') {
                name_[index] = name[index];
                ++index;
            }
            name_[index] = '\0';
            tag_ = contract_tag;
            context_ = context;
            rx_.set_callbacks(irq, deferred, context);
            tx_.initialize(context);
            running_ = false;
        }

        static constexpr u64 contract_tag = 0x4E45544445563101ull;
        bool valid() const { return tag_ == contract_tag && name_[0] != '\0'; }
        const char* name() const { return name_; }

        void set_mac(const u8* value) {
            if (value == nullptr)
                return;
            for (u32 i = 0; i < 6; ++i)
                mac_[i] = value[i];
        }
        const u8* mac() const { return mac_; }
        void set_mtu(u16 value) { mtu_ = value; }
        u16 mtu() const { return mtu_; }
        void set_link_flags(u32 value) { link_flags_ = value; }
        u32 link_flags() const { return link_flags_; }
        void set_generation(u64 value) { tx_.set_generation(value); }
        u64 generation() const { return tx_.generation(); }
        void set_running(bool value) { running_ = value; }
        bool running() const { return running_; }
        void* module_context() const { return context_; }

        rx_action dispatch_rx(packet_view packet) { return rx_.dispatch_irq(packet); }
        rx_action invoke_rx_irq(packet_view& packet) const { return rx_.invoke_irq(packet); }
        void invoke_rx_deferred(packet_view& packet) const { rx_.invoke_deferred(packet); }
        u32 poll_rx() { return rx_.poll(); }
        bool rx_deferred_full() const { return rx_.deferred_full(); }
        void set_callbacks(rx_irq_callback irq, rx_deferred_callback deferred, void* context) {
            rx_.set_callbacks(irq, deferred, context);
            context_ = context;
            tx_.set_context(context);
        }
        tx_path<tx_pending_depth, tx_completion_depth>& tx() { return tx_; }

        private:
        char name_[16]{};
        u64 tag_ = contract_tag;
        u8 mac_[6]{};
        u16 mtu_ = 1500;
        u32 link_flags_ = 0;
        bool running_ = false;
        void* context_ = nullptr;
        rx_path rx_;
        tx_path<tx_pending_depth, tx_completion_depth> tx_;
    };

    class registry final {
        public:
        static constexpr u32 max_devices = 32;

        bool register_device(netdev& device) {
            for (u32 i = 0; i < count_; ++i)
                if (devices_[i] == &device)
                    return true;
            if (!device.valid() || count_ == max_devices)
                return false;
            devices_[count_++] = &device;
            return true;
        }

        bool unregister_device(netdev& device) {
            for (u32 i = 0; i < count_; ++i) {
                if (devices_[i] != &device)
                    continue;
                devices_[i] = devices_[--count_];
                devices_[count_] = nullptr;
                return true;
            }
            return false;
        }

        bool contains(const netdev& device) const {
            for (u32 i = 0; i < count_; ++i)
                if (devices_[i] == &device)
                    return true;
            return false;
        }

        netdev* at(u32 index) const { return index < count_ ? devices_[index] : nullptr; }
        u32 count() const { return count_; }

        private:
        netdev* devices_[max_devices]{};
        u32 count_ = 0;
    };

    using net_device = netdev;

    class netdev_handle final {
        public:
        netdev_handle() = default;
        netdev_handle(registry& owner, netdev& value) : owner_(&owner), value_(&value) {
            if (!owner_->contains(*value_))
                active_ = owner_->register_device(*value_);
        }
        netdev_handle(const netdev_handle&) = delete;
        netdev_handle& operator=(const netdev_handle&) = delete;
        netdev_handle(netdev_handle&& other) noexcept
            : owner_(other.owner_), value_(other.value_), active_(other.active_) {
            other.owner_ = nullptr;
            other.value_ = nullptr;
            other.active_ = false;
        }
        netdev_handle& operator=(netdev_handle&& other) noexcept {
            if (this != &other) {
                reset();
                owner_ = other.owner_;
                value_ = other.value_;
                active_ = other.active_;
                other.owner_ = nullptr;
                other.value_ = nullptr;
                other.active_ = false;
            }
            return *this;
        }
        ~netdev_handle() { reset(); }

        explicit operator bool() const { return active_; }
        netdev* get() const { return active_ ? value_ : nullptr; }
        void reset() {
            if (active_ && owner_ != nullptr && value_ != nullptr)
                owner_->unregister_device(*value_);
            owner_ = nullptr;
            value_ = nullptr;
            active_ = false;
        }

        private:
        registry* owner_ = nullptr;
        netdev* value_ = nullptr;
        bool active_ = false;
    };

    registry& devices();

} // namespace astia::net
