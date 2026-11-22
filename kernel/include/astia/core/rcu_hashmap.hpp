#pragma once

#include <astia/core/rcu_grace.hpp>
#include <astia/mm/shptr.hpp>
#include <astia/optional.hpp>

namespace astia::core {

    /// Sharded immutable hashmap with lockless readers and CAS writers.
    template <typename Key, typename Value, usize Shards = 8, usize Retired = 8,
              typename Hash = u64 (*)(const Key&)>
    class rcu_hashmap {
        static_assert(Shards != 0, "rcu_hashmap needs shards");
        static constexpr usize bucket_count = 16;

        struct node {
            Key             key;
            Value           value;
            mm::shptr<node> next;

            node(const Key& k, const Value& v, const mm::shptr<node>& n)
                : key(k), value(v), next(n) {}
        };
        struct snapshot {
            mm::shptr<node> buckets[bucket_count]{};
        };
        using grace = rcu_grace_period<snapshot>;
        struct shard {
            grace     grace_period;
            snapshot* published = nullptr;
        };

        Hash hash_;
        bool (*equal_)(const Key&, const Key&);
        mutable shard shards_[Shards];

        usize shard_index(const Key& key) const {
            return static_cast<usize>(hash_(key) % Shards);
        }
        usize bucket_index(const Key& key) const {
            return static_cast<usize>(hash_(key) % bucket_count);
        }

        snapshot* clone_without(const snapshot* old, const Key& key) const {
            auto* next = new snapshot{};
            if (!next)
                return {};
            for (usize bucket = 0; bucket < bucket_count; ++bucket) {
                const node* item = old == nullptr ? nullptr : old->buckets[bucket].get();
                for (; item != nullptr; item = item->next.get()) {
                    if (equal_(item->key, key))
                        continue;
                    auto copy = mm::shptr<node>(
                        new node(item->key, item->value, next->buckets[bucket]));
                    if (!copy)
                        return {};
                    next->buckets[bucket] = static_cast<mm::shptr<node>&&>(copy);
                }
            }
            return next;
        }

        public:
        class read_guard {
            public:
            read_guard(const rcu_hashmap& map, usize shard_index)
                : map_(map), shard_index_(shard_index) {
                auto& state = map_.shards_[shard_index_];
                state.grace_period.enter();
                snapshot_ = __atomic_load_n(&state.published, __ATOMIC_SEQ_CST);
            }
            ~read_guard() {
                map_.shards_[shard_index_].grace_period.leave();
            }
            read_guard(const read_guard&)            = delete;
            read_guard& operator=(const read_guard&) = delete;

            const Value* find(const Key& key) const {
                if (snapshot_ == nullptr)
                    return nullptr;
                for (const node* item = snapshot_->buckets[map_.bucket_index(key)].get();
                     item != nullptr; item = item->next.get())
                    if (map_.equal_(item->key, key))
                        return &item->value;
                return nullptr;
            }

            private:
            const rcu_hashmap& map_;
            usize              shard_index_;
            snapshot*          snapshot_ = nullptr;
        };

        rcu_hashmap(Hash hash, bool (*equal)(const Key&, const Key&))
            : hash_(hash), equal_(equal) {}
        ~rcu_hashmap() {
            for (shard& state : shards_) {
                state.grace_period.discard();
                delete __atomic_exchange_n(&state.published, nullptr, __ATOMIC_SEQ_CST);
            }
        }
        rcu_hashmap(const rcu_hashmap&)            = delete;
        rcu_hashmap& operator=(const rcu_hashmap&) = delete;

        optional<Value> find(const Key& key) const {
            read_guard   guard(*this, shard_index(key));
            const Value* value = guard.find(key);
            return value == nullptr ? optional<Value>{} : optional<Value>(*value);
        }

        bool insert_or_assign(const Key& key, const Value& value) {
            return update(key, value, false);
        }

        bool erase(const Key& key) {
            return update(key, Value{}, true);
        }

        private:
        bool update(const Key& key, const Value& value, bool remove) {
            shard& state = shards_[shard_index(key)];
            for (;;) {
                state.grace_period.reclaim();
                state.grace_period.enter();
                snapshot* old = __atomic_load_n(&state.published, __ATOMIC_SEQ_CST);

                snapshot* next   = clone_without(old, key);
                auto      record = new grace::retired_record{};
                if (!next || record == nullptr) {
                    delete next;
                    delete record;
                    state.grace_period.leave();
                    return false;
                }
                if (!remove) {
                    const usize bucket = bucket_index(key);
                    auto        item =
                        mm::shptr<node>(new node(key, value, next->buckets[bucket]));
                    if (!item) {
                        delete next;
                        delete record;
                        state.grace_period.leave();
                        return false;
                    }
                    next->buckets[bucket] = static_cast<mm::shptr<node>&&>(item);
                }

                snapshot* expected = old;
                if (__atomic_compare_exchange_n(&state.published, &expected, next, false,
                                                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
                    if (old != nullptr)
                        state.grace_period.retire(old, record);
                    else
                        delete record;
                    state.grace_period.leave();
                    return true;
                }
                delete record;
                delete next;
                state.grace_period.leave();
                // `next` still owns the failed candidate and releases it here.
            }
        }
    };

} // namespace astia::core
