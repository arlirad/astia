#pragma once

#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>
#include <astia/atomic.hpp>
#include <astia/common.hpp>
#include <astia/mm/shptr.hpp>
#include <astia/net/address.hpp>
#include <astia/net/types.hpp>

namespace astia::net {
    class link;

    struct packet_segment {
        const u8* data   = nullptr;
        u32       length = 0;
    };

    struct packet_view {
        packet_segment first{};
        packet_segment second{};
        u32            length                    = 0;
        u32            wire_length               = 0;
        u64            generation                = 0;
        void*          owner                     = nullptr;
        void (*recycle)(void* owner, int status) = nullptr;

        /// Consume `bytes` from the front.  `wire_length` keeps the original
        /// on-wire length for diagnostics.
        bool advance(u32 bytes) {
            if (bytes > first.length + second.length)
                return false;

            if (bytes <= first.length) {
                first.data += bytes;
                first.length -= bytes;
            }
            else {
                const u32 remaining = bytes - first.length;

                first = second;
                first.data += remaining;
                first.length -= remaining;
                second = {};
            }

            length = first.length + second.length;

            return true;
        }
    };

    struct tx_request;
    using tx_reclaim_fn = void (*)(void*, const tx_request*, int);

    struct tx_request {
        const u8*     data       = nullptr;
        u64           physical   = 0;
        u32           length     = 0;
        u64           generation = 0;
        void*         cookie     = nullptr;
        tx_reclaim_fn reclaim    = nullptr;
    };

    enum link_flags : u32 {
        link_up          = 1u << 0,
        link_running     = 1u << 1,
        link_full_duplex = 1u << 2,
    };

    enum class device_state : u8 {
        detached,
        discovered,
        running,
        stopping,
        reset,
    };

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

        T           _values[Capacity]{};
        mutable u32 _head = 0;
        mutable u32 _tail = 0;

        public:
        bounded_spsc_queue() {
            initialize();
        }

        bounded_spsc_queue(const bounded_spsc_queue&)            = delete;
        bounded_spsc_queue& operator=(const bounded_spsc_queue&) = delete;

        void initialize() {
            atomic_ref<u32>(_head).store(0, memory_order::relaxed);
            atomic_ref<u32>(_tail).store(0, memory_order::relaxed);
        }

        bool push(const T& value) {
            const u32 head = atomic_ref<u32>(_head).load(memory_order::relaxed);
            const u32 tail = atomic_ref<u32>(_tail).load(memory_order::acquire);

            if (head - tail == Capacity)
                return false;

            _values[head & (Capacity - 1u)] = value;

            atomic_ref<u32>(_head).store(head + 1u, memory_order::release);

            return true;
        }

        bool pop(T& value) {
            const u32 tail = atomic_ref<u32>(_tail).load(memory_order::relaxed);
            const u32 head = atomic_ref<u32>(_head).load(memory_order::acquire);

            if (tail == head)
                return false;

            value = _values[tail & (Capacity - 1u)];

            atomic_ref<u32>(_tail).store(tail + 1u, memory_order::release);

            return true;
        }

        u32 size() const {
            return atomic_ref<u32>(_head).load(memory_order::acquire) -
                   atomic_ref<u32>(_tail).load(memory_order::acquire);
        }
    };

    template <typename T, u32 Capacity>
    class bounded_mpsc_queue final {
        static_assert(Capacity >= 2 && (Capacity & (Capacity - 1u)) == 0);

        struct cell {
            u32 sequence = 0;
            T   value{};
        };

        cell _cells[Capacity]{};
        u32  _enqueue_position = 0;
        u32  _dequeue_position = 0;

        public:
        bounded_mpsc_queue() {
            initialize();
        }

        bounded_mpsc_queue(const bounded_mpsc_queue&)            = delete;
        bounded_mpsc_queue& operator=(const bounded_mpsc_queue&) = delete;

        void initialize() {
            atomic_ref<u32>(_enqueue_position).store(0, memory_order::relaxed);
            atomic_ref<u32>(_dequeue_position).store(0, memory_order::relaxed);

            for (u32 i = 0; i < Capacity; ++i) {
                atomic_ref<u32>(_cells[i].sequence).store(i, memory_order::relaxed);
            }
        }

        bool push(const T& value) {
            u32 position = atomic_ref<u32>(_enqueue_position).load(memory_order::relaxed);

            while (true) {
                cell& slot = _cells[position & (Capacity - 1u)];

                const u32 sequence =
                    atomic_ref<u32>(slot.sequence).load(memory_order::acquire);
                const i32 difference = static_cast<i32>(sequence - position);
                if (difference == 0) {
                    if (atomic_ref<u32>(_enqueue_position)
                            .compare_exchange_strong(position, position + 1u,
                                                     memory_order::relaxed,
                                                     memory_order::relaxed)) {
                        slot.value = value;

                        atomic_ref<u32>(slot.sequence)
                            .store(position + 1u, memory_order::release);

                        return true;
                    }
                }
                else if (difference < 0) {
                    return false;
                }
                else {
                    position =
                        atomic_ref<u32>(_enqueue_position).load(memory_order::relaxed);
                }
            }
        }

        bool pop(T& value) {
            const u32 position =
                atomic_ref<u32>(_dequeue_position).load(memory_order::relaxed);

            cell& slot = _cells[position & (Capacity - 1u)];

            const u32 sequence =
                atomic_ref<u32>(slot.sequence).load(memory_order::acquire);

            if (static_cast<i32>(sequence - (position + 1u)) < 0)
                return false;

            value = slot.value;

            atomic_ref<u32>(slot.sequence)
                .store(position + Capacity, memory_order::release);
            atomic_ref<u32>(_dequeue_position)
                .store(position + 1u, memory_order::relaxed);

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
            int        status = 0;
        };

        bounded_mpsc_queue<tx_request, PendingDepth>    _pending;
        bounded_spsc_queue<completion, CompletionDepth> _completed;
        mutable u64                                     _generation    = 1;
        mutable u32                                     _accepting     = 1;
        u32                                             _submitters    = 0;
        u32                                             _completers    = 0;
        u32                                             _consumer_lock = 0;

        void lock_consumer() {
            u32 expected = 0;

            while (!atomic_ref<u32>(_consumer_lock)
                        .compare_exchange_strong(expected, 1, memory_order::acquire,
                                                 memory_order::relaxed)) {
                expected = 0;

                arch::cpu::relax();
            }
        }

        void unlock_consumer() {
            atomic_ref<u32>(_consumer_lock).store(0, memory_order::release);
        }

        void reclaim(tx_request& request, int status) {
            if (request.reclaim != nullptr) {
                const auto callback = request.reclaim;

                request.reclaim = nullptr;

                callback(_context, &request, status);
            }
        }

        void stop_accepting() {
            atomic_ref<u32>(_accepting).store(0, memory_order::release);

            while (atomic_ref<u32>(_submitters).load(memory_order::acquire) != 0 ||
                   atomic_ref<u32>(_completers).load(memory_order::acquire) != 0) {
                arch::cpu::relax();
            }
        }

        void start_accepting() {
            atomic_ref<u32>(_accepting).store(1, memory_order::release);
        }

        public:
        explicit tx_path(void* context = nullptr) {
            initialize(context);
        }

        tx_path(const tx_path&)            = delete;
        tx_path& operator=(const tx_path&) = delete;

        void initialize(void* context = nullptr) {
            _pending.initialize();
            _completed.initialize();
            atomic_ref<u64>(_generation).store(1, memory_order::relaxed);
            atomic_ref<u32>(_accepting).store(1, memory_order::relaxed);
            atomic_ref<u32>(_submitters).store(0, memory_order::relaxed);
            atomic_ref<u32>(_completers).store(0, memory_order::relaxed);
            atomic_ref<u32>(_consumer_lock).store(0, memory_order::relaxed);

            _context = context;
        }

        int submit(tx_request request) {
            if (request.data == nullptr || request.length == 0 ||
                request.reclaim == nullptr)
                return -EINVAL;

            if (atomic_ref<u32>(_accepting).load(memory_order::acquire) == 0)
                return -ECANCELED;

            atomic_ref<u32>(_submitters).add_fetch(1, memory_order::acquire);

            if (atomic_ref<u32>(_accepting).load(memory_order::acquire) == 0) {
                atomic_ref<u32>(_submitters).sub_fetch(1, memory_order::release);

                return -ECANCELED;
            }

            request.generation = atomic_ref<u64>(_generation).load(memory_order::acquire);

            const bool pushed = _pending.push(request);

            atomic_ref<u32>(_submitters).sub_fetch(1, memory_order::release);

            return pushed ? 0 : -EAGAIN;
        }

        bool dequeue(tx_request& request) {
            lock_consumer();

            const bool result = _pending.pop(request);

            unlock_consumer();

            return result;
        }

        bool complete(const tx_request& request, int status) {
            atomic_ref<u32>(_completers).add_fetch(1, memory_order::acquire);

            if (atomic_ref<u32>(_accepting).load(memory_order::acquire) == 0 ||
                request.generation !=
                    atomic_ref<u64>(_generation).load(memory_order::acquire)) {
                atomic_ref<u32>(_completers).sub_fetch(1, memory_order::release);

                return false;
            }

            const bool pushed = _completed.push({request, status});

            atomic_ref<u32>(_completers).sub_fetch(1, memory_order::release);

            return pushed;
        }

        bool cancel(const tx_request& request) {
            const u64 generation =
                atomic_ref<u64>(_generation).load(memory_order::acquire);

            if (request.generation != 0 && request.generation != generation)
                return false;

            stop_accepting();
            lock_consumer();

            tx_request pending{};

            bool found = false;

            bounded_spsc_queue<tx_request, PendingDepth> retained;

            while (_pending.pop(pending)) {
                if (!found && pending.cookie == request.cookie &&
                    pending.generation == generation) {
                    reclaim(pending, -ECANCELED);

                    found = true;
                }
                else {
                    retained.push(pending);
                }
            }

            while (retained.pop(pending)) {
                _pending.push(pending);
            }

            unlock_consumer();
            start_accepting();

            return found;
        }

        u32 poll() {
            lock_consumer();

            u32 count = 0;

            completion item{};

            while (_completed.pop(item)) {
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

            while (_pending.pop(request)) {
                reclaim(request, -ECANCELED);
            }

            completion item{};

            while (_completed.pop(item)) {
                reclaim(item.request, -ECANCELED);
            }

            u64 generation = atomic_ref<u64>(_generation).load(memory_order::relaxed) + 1;
            if (generation == 0)
                generation = 1;

            atomic_ref<u64>(_generation).store(generation, memory_order::release);
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

        void set_context(void* context) {
            _context = context;
        }

        bool accepting() const {
            return atomic_ref<u32>(_accepting).load(memory_order::acquire) != 0;
        }

        void set_generation(u64 value) {
            atomic_ref<u64>(_generation)
                .store(value == 0 ? 1 : value, memory_order::release);
        }

        u64 generation() const {
            return atomic_ref<u64>(_generation).load(memory_order::acquire);
        }

        private:
        void* _context = nullptr;
    };

    class netdev final {
        public:
        static constexpr u32 tx_pending_depth    = 16;
        static constexpr u32 tx_completion_depth = 4;

        netdev(const char* name, void* context = nullptr)
            : _context(context), _tx(context) {
            initialize(name, context);
        }

        void initialize(const char* name, void* context = nullptr) {
            _name[0] = '\0';

            u32 index = 0;

            while (index < sizeof(_name) - 1 && name != nullptr && name[index] != '\0') {
                _name[index] = name[index];

                ++index;
            }

            _name[index] = '\0';
            _tag         = contract_tag;
            _context     = context;

            _tx.initialize(context);

            _address = {};
            _kind    = link_kind::unspecified;
            _link    = {};
            _running = false;
        }

        static constexpr u64 contract_tag = 0x4E45544445563101ull;

        bool valid() const {
            return _tag == contract_tag && _name[0] != '\0';
        }

        const char* name() const {
            return _name;
        }

        void set_address(const link_address& value) {
            _address = value;
        }

        const link_address& address() const {
            return _address;
        }

        void set_kind(link_kind value) {
            _kind = value;
        }

        link_kind kind() const {
            return _kind;
        }

        void set_mac(const u8* value) {
            if (value == nullptr)
                return;

            _address = link_address::from_packet(value, 6);
        }

        const u8* mac() const {
            return _address.length == 6 ? _address.bytes : nullptr;
        }

        void set_mtu(u16 value) {
            _mtu = value;
        }

        net::mtu mtu() const {
            return net::mtu{_mtu};
        }

        void set_link_flags(u32 value) {
            _link_flags = value;
        }

        u32 link_flags() const {
            return _link_flags;
        }

        void set_generation(u64 value) {
            _tx.set_generation(value);
        }

        u64 generation() const {
            return _tx.generation();
        }

        void set_running(bool value) {
            _running = value;
        }

        bool running() const {
            return _running;
        }

        void* module_context() const {
            return _context;
        }

        /// Binds the L2 implementation.  netdev keeps a strong reference; the
        /// link keeps only a weak reference back to the device.
        bool attach(const mm::shptr<link>& value) {
            if (_link || !value)
                return false;

            _link = value;

            return true;
        }

        void detach() {
            _link = {};
        }

        link* attached() const {
            return _link.get();
        }

        /// Hands a borrowed packet_view to the attached link; the caller
        /// recycles it after this returns.  Defined in net.cpp.
        void dispatch_rx(packet_view packet);

        tx_path<tx_pending_depth, tx_completion_depth>& tx() {
            return _tx;
        }

        private:
        char                                           _name[16]{};
        u64                                            _tag = contract_tag;
        link_address                                   _address{};
        link_kind                                      _kind = link_kind::unspecified;
        mm::shptr<link>                                _link{};
        u16                                            _mtu        = 1500;
        u32                                            _link_flags = 0;
        bool                                           _running    = false;
        void*                                          _context    = nullptr;
        tx_path<tx_pending_depth, tx_completion_depth> _tx;
    };

    class registry final {
        public:
        static constexpr u32 max_devices = 32;

        bool register_device(netdev& device) {
            for (u32 i = 0; i < _count; ++i) {
                if (_devices[i].get() == &device)
                    return true;
            }

            if (!device.valid() || _count == max_devices)
                return false;

            _devices[_count++] = mm::shptr<netdev>::borrowed(&device);

            return true;
        }

        bool unregister_device(netdev& device) {
            for (u32 i = 0; i < _count; ++i) {
                if (_devices[i].get() != &device)
                    continue;

                _devices[i]      = _devices[--_count];
                _devices[_count] = {};

                return true;
            }

            return false;
        }

        bool contains(const netdev& device) const {
            for (u32 i = 0; i < _count; ++i) {
                if (_devices[i].get() == &device)
                    return true;
            }

            return false;
        }

        netdev* at(u32 index) const {
            return index < _count ? _devices[index].get() : nullptr;
        }

        u32 count() const {
            return _count;
        }

        private:
        mm::shptr<netdev> _devices[max_devices]{};
        u32               _count = 0;
    };

    using net_device = netdev;

    registry& devices();
} // namespace astia::net
