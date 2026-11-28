#pragma once

#include <astia/common.hpp>

namespace astia::proc {
    template <typename Value, u32 Capacity>
    class run_queue {
        static_assert(Capacity < 0xFE);

        public:
        static constexpr u32 none = ~u32{0};

        struct policy {
            u64  vruntime = 0;
            u8   priority = 0;
            bool realtime = false;
        };

        u32 size() const {
            return _count;
        }

        bool empty() const {
            return _count == 0;
        }

        bool contains(u32 slot) const {
            return slot < Capacity && _slots[slot].value != nullptr;
        }

        bool is_active(u32 slot) const {
            return contains(slot) && _slots[slot].active;
        }

        Value* value_at(u32 slot) const {
            return contains(slot) ? _slots[slot].value : nullptr;
        }

        u64 sequence_at(u32 slot) const {
            return contains(slot) ? _slots[slot].sequence : 0;
        }

        u64 probes() const {
            return _probes;
        }

        u32 insert(Value* value, const policy& bounds) {
            if (value == nullptr)
                return none;

            const u32 slot = allocate();
            if (slot == none)
                return none;

            auto& entry = _slots[slot];

            entry.value      = value;
            entry.vruntime   = bounds.vruntime;
            entry.priority   = bounds.priority;
            entry.realtime   = bounds.realtime;
            entry.sequence   = ++_sequence;
            entry.next       = none;
            entry.prev       = none;
            entry.heap_index = none;

            ++_count;
            link(slot);

            return slot;
        }

        bool reactivate(u32 slot, const policy& bounds) {
            if (!contains(slot))
                return false;

            auto& entry = _slots[slot];

            if (entry.active) {
                if (!same_policy(entry, bounds))
                    unlink(slot);
                else
                    return true;
            }

            entry.vruntime = bounds.vruntime;
            entry.priority = bounds.priority;
            entry.realtime = bounds.realtime;

            link(slot);

            return true;
        }

        void erase(u32 slot) {
            if (!contains(slot))
                return;

            auto& entry = _slots[slot];

            if (entry.active)
                unlink(slot);

            entry.value = nullptr;
            entry.next  = _free;
            _free       = slot;

            --_count;
        }

        template <typename Eligible>
        Value* pick(Eligible&& eligible) {
            if (Value* value = pick_realtime(eligible))
                return value;

            return pick_normal(eligible);
        }

        struct admission {
            Value* next   = nullptr;
            u32    slot   = none;
            bool   queued = false;
        };

        /// Insert `value` for another turn.  At capacity the entry `pick`
        /// would have chosen next is detached first and returned in `next`
        /// for the caller to run immediately, so a saturated queue hands off
        /// a turn instead of dropping one.
        template <typename Eligible>
        admission insert_or_handoff(Value* value, const policy& bounds,
                                    Eligible&& eligible) {
            admission result;

            if (value == nullptr)
                return result;

            if (_count < Capacity) {
                result.slot   = insert(value, bounds);
                result.queued = result.slot != none;

                return result;
            }

            result.next = pick(eligible);

            if (result.next == nullptr)
                return result;

            // pick() released a slot, so this insert always succeeds.
            result.slot   = insert(value, bounds);
            result.queued = result.slot != none;

            return result;
        }

        template <typename Function>
        void for_each(Function&& function) const {
            for (u32 slot = 0; slot < Capacity; ++slot) {
                const auto& entry = _slots[slot];

                if (entry.value != nullptr)
                    function(slot, entry.value, entry.sequence, entry.active);
            }
        }

        template <typename Predicate>
        bool any(Predicate&& predicate) const {
            for (u32 slot = 0; slot < Capacity; ++slot) {
                const auto& entry = _slots[slot];

                if (entry.value != nullptr && entry.active && predicate(entry.value))
                    return true;
            }

            return false;
        }

        private:
        struct slot {
            Value* value      = nullptr;
            u64    sequence   = 0;
            u64    vruntime   = 0;
            u32    next       = none;
            u32    prev       = none;
            u32    heap_index = none;
            u8     priority   = 0;
            bool   realtime   = false;
            bool   active     = false;
        };

        static constexpr u32 rt_priority_count = 256;
        static constexpr u32 rt_word_count     = rt_priority_count / 64;
        static constexpr u8  bucket_none       = 0xFF;

        u32 allocate() {
            if (_free != none) {
                const u32 slot = _free;

                _free = _slots[slot].next;

                return slot;
            }

            if (_next_slot == Capacity)
                return none;

            return _next_slot++;
        }

        static bool same_policy(const slot& entry, const policy& bounds) {
            if (entry.realtime != bounds.realtime)
                return false;

            if (bounds.realtime)
                return entry.priority == bounds.priority;

            return entry.vruntime == bounds.vruntime;
        }

        void link(u32 slot) {
            auto& entry = _slots[slot];

            if (entry.realtime)
                link_realtime(slot);
            else
                heap_insert(slot);

            entry.active = true;
        }

        void unlink(u32 slot) {
            auto& entry = _slots[slot];

            if (entry.realtime)
                unlink_realtime(slot);
            else
                heap_remove(entry.heap_index);

            entry.active = false;
        }

        template <typename Eligible>
        Value* pick_realtime(Eligible& eligible) {
            while (true) {
                const i32 priority = highest_priority();
                if (priority < 0)
                    return nullptr;

                const u32 slot = _rt_head[priority];
                if (slot == bucket_none) {
                    clear_bit(static_cast<u32>(priority));

                    continue;
                }

                Value* value = _slots[slot].value;

                ++_probes;

                if (eligible(value))
                    return detach(slot);

                defer(slot);
            }
        }

        template <typename Eligible>
        Value* pick_normal(Eligible& eligible) {
            while (_heap_count != 0) {
                const u32 slot = _heap[0];

                Value* value = _slots[slot].value;

                ++_probes;

                if (eligible(value))
                    return detach(slot);

                defer(slot);
            }

            return nullptr;
        }

        Value* detach(u32 slot) {
            Value* value = _slots[slot].value;

            erase(slot);

            return value;
        }

        void defer(u32 slot) {
            if (_slots[slot].active)
                unlink(slot);
        }

        i32 highest_priority() const {
            for (i32 word = static_cast<i32>(rt_word_count) - 1; word >= 0; --word) {
                const u64 bits = _rt_bitmap[static_cast<u32>(word)];
                if (bits != 0)
                    return word * 64 + (63 - static_cast<i32>(__builtin_clzll(bits)));
            }

            return -1;
        }

        void set_bit(u32 priority) {
            _rt_bitmap[priority / 64] |= u64{1} << (priority % 64);
        }

        void clear_bit(u32 priority) {
            _rt_bitmap[priority / 64] &= ~(u64{1} << (priority % 64));
        }

        bool test_bit(u32 priority) const {
            return (_rt_bitmap[priority / 64] & (u64{1} << (priority % 64))) != 0;
        }

        u32 realtime_head(u32 priority) const {
            return _rt_head[priority] == bucket_none ? none : _rt_head[priority];
        }

        void link_realtime(u32 slot) {
            auto& entry = _slots[slot];

            const u32 priority = entry.priority;

            if (!test_bit(priority)) {
                entry.next         = slot;
                entry.prev         = slot;
                _rt_head[priority] = static_cast<u8>(slot);

                set_bit(priority);

                return;
            }

            const u32 head = realtime_head(priority);
            const u32 tail = _slots[head].prev;

            if (_slots[tail].sequence < entry.sequence) {
                entry.next        = head;
                entry.prev        = tail;
                _slots[tail].next = slot;
                _slots[head].prev = slot;

                return;
            }

            u32 cursor = tail;

            while (cursor != head && _slots[cursor].sequence > entry.sequence) {
                cursor = _slots[cursor].prev;
            }

            if (_slots[cursor].sequence < entry.sequence) {
                const u32 next = _slots[cursor].next;

                entry.next          = next;
                entry.prev          = cursor;
                _slots[cursor].next = slot;
                _slots[next].prev   = slot;

                return;
            }

            entry.next         = head;
            entry.prev         = tail;
            _slots[tail].next  = slot;
            _slots[head].prev  = slot;
            _rt_head[priority] = static_cast<u8>(slot);
        }

        void unlink_realtime(u32 slot) {
            auto& entry = _slots[slot];

            const u32 priority = entry.priority;
            const u32 next     = entry.next;
            const u32 prev     = entry.prev;

            if (next == slot) {
                _rt_head[priority] = bucket_none;

                clear_bit(priority);
            }
            else {
                _slots[prev].next = next;
                _slots[next].prev = prev;

                if (realtime_head(priority) == slot)
                    _rt_head[priority] = static_cast<u8>(next);
            }

            entry.next = none;
            entry.prev = none;
        }

        bool before(u32 left, u32 right) const {
            const auto& first  = _slots[left];
            const auto& second = _slots[right];

            if (first.vruntime != second.vruntime)
                return first.vruntime < second.vruntime;

            return first.sequence < second.sequence;
        }

        void heap_swap(u32 left, u32 right) {
            const u32 first  = _heap[left];
            const u32 second = _heap[right];

            _heap[left]  = second;
            _heap[right] = first;

            _slots[first].heap_index  = right;
            _slots[second].heap_index = left;
        }

        void heap_up(u32 index) {
            while (index > 0) {
                const u32 parent = (index - 1) / 2;

                if (!before(_heap[index], _heap[parent]))
                    return;

                heap_swap(index, parent);

                index = parent;
            }
        }

        void heap_down(u32 index) {
            while (true) {
                u32       smallest = index;
                const u32 left     = index * 2 + 1;
                const u32 right    = left + 1;

                if (left < _heap_count && before(_heap[left], _heap[smallest]))
                    smallest = left;

                if (right < _heap_count && before(_heap[right], _heap[smallest]))
                    smallest = right;

                if (smallest == index)
                    return;

                heap_swap(index, smallest);

                index = smallest;
            }
        }

        void heap_insert(u32 slot) {
            const u32 index = _heap_count++;

            _heap[index]            = slot;
            _slots[slot].heap_index = index;

            heap_up(index);
        }

        void heap_remove(u32 index) {
            const u32 removed = _heap[index];

            --_heap_count;

            if (index != _heap_count) {
                _heap[index]                    = _heap[_heap_count];
                _slots[_heap[index]].heap_index = index;

                if (index > 0 && before(_heap[index], _heap[(index - 1) / 2]))
                    heap_up(index);
                else
                    heap_down(index);
            }

            _slots[removed].heap_index = none;
        }

        slot _slots[Capacity]{};
        u32  _heap[Capacity]{};
        u32  _heap_count = 0;
        u32  _count      = 0;
        u32  _next_slot  = 0;
        u32  _free       = none;
        u64  _sequence   = 0;
        u64  _probes     = 0;
        u64  _rt_bitmap[rt_word_count]{};
        u8   _rt_head[rt_priority_count]{};
    };
} // namespace astia::proc
