#pragma once

#include <astia/optional.hpp>
#include <astia/mm/shptr.hpp>

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
        struct retired_snapshot {
            snapshot*        value;
            retired_snapshot* next;
        };
        struct shard {
            mutable u32      readers = 0;
            snapshot*         published = nullptr;
            mutable retired_snapshot* retired = nullptr;
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

        void reclaim(shard& state) const {
            if (__atomic_load_n(&state.readers, __ATOMIC_SEQ_CST) != 0)
                return;
            auto* list = __atomic_exchange_n(&state.retired, nullptr, __ATOMIC_SEQ_CST);
            while (list != nullptr) {
                auto* next = list->next;
                delete list->value;
                delete list;
                list = next;
            }
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
                    auto copy = mm::shptr<node>(new node(item->key, item->value,
                                                         next->buckets[bucket]));
                    if (!copy)
                        return {};
                    next->buckets[bucket] = static_cast<mm::shptr<node>&&>(copy);
                }
            }
            return next;
        }

        void retire(shard& state, snapshot* old, retired_snapshot* record) const {
            record->value = old;
            auto* head = __atomic_load_n(&state.retired, __ATOMIC_RELAXED);
            do {
                record->next = head;
            } while (!__atomic_compare_exchange_n(&state.retired, &head, record, false,
                                                  __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
        }

        public:
        class read_guard {
            public:
            read_guard(const rcu_hashmap& map, usize shard_index)
                : map_(map), shard_index_(shard_index) {
                auto& state = map_.shards_[shard_index_];
                __atomic_add_fetch(&state.readers, 1, __ATOMIC_SEQ_CST);
                snapshot_ = __atomic_load_n(&state.published, __ATOMIC_SEQ_CST);
            }
            ~read_guard() {
                __atomic_sub_fetch(&map_.shards_[shard_index_].readers, 1,
                                   __ATOMIC_SEQ_CST);
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
                __atomic_store_n(&state.readers, 0, __ATOMIC_SEQ_CST);
                reclaim(state);
                delete __atomic_exchange_n(&state.published, nullptr, __ATOMIC_SEQ_CST);
            }
        }
        rcu_hashmap(const rcu_hashmap&)            = delete;
        rcu_hashmap& operator=(const rcu_hashmap&) = delete;

        optional<Value> find(const Key& key) const {
            read_guard guard(*this, shard_index(key));
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
                reclaim(state);
                __atomic_add_fetch(&state.readers, 1, __ATOMIC_SEQ_CST);
                snapshot* old = __atomic_load_n(&state.published, __ATOMIC_SEQ_CST);

                snapshot* next = clone_without(old, key);
                auto record = new retired_snapshot{};
                if (!next || record == nullptr) {
                    delete next;
                    delete record;
                    __atomic_sub_fetch(&state.readers, 1, __ATOMIC_SEQ_CST);
                    return false;
                }
                if (!remove) {
                    const usize bucket = bucket_index(key);
                    auto item = mm::shptr<node>(new node(key, value, next->buckets[bucket]));
                    if (!item) {
                        delete next;
                        delete record;
                        __atomic_sub_fetch(&state.readers, 1, __ATOMIC_SEQ_CST);
                        return false;
                    }
                    next->buckets[bucket] = static_cast<mm::shptr<node>&&>(item);
                }

                snapshot* expected = old;
                if (__atomic_compare_exchange_n(&state.published, &expected, next, false,
                                                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
                    if (old != nullptr)
                        retire(state, old, record);
                    else
                        delete record;
                    __atomic_sub_fetch(&state.readers, 1, __ATOMIC_SEQ_CST);
                    return true;
                }
                delete record;
                delete next;
                __atomic_sub_fetch(&state.readers, 1, __ATOMIC_SEQ_CST);
                // `next` still owns the failed candidate and releases it here.
            }
        }
    };

} // namespace astia::core
