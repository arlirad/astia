#pragma once
#include <astia/core/spinlock.hpp>
#include <astia/mm/shptr.hpp>
#include <astia/optional.hpp>
namespace astia::core {
    template <typename Key, typename Value, usize Shards = 8, usize Retired = 8,
              typename Hash = u64 (*)(const Key&)>
    class rcu_hashmap {
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
        struct shard {
            core::spinlock      writer;
            mm::shptr<snapshot> current;
            mm::shptr<snapshot> retired[Retired]{};
            usize               retired_count = 0;
            u32                 readers       = 0;
            snapshot*           published     = nullptr;
        };
        Hash hash_;
        bool (*equal_)(const Key&, const Key&);
        mutable shard shards_[Shards];
        usize         shard_of(const Key& k) const {
            return hash_(k) % Shards;
        }
        usize bucket_of(const Key& k) const {
            return hash_(k) % bucket_count;
        }
        void reclaim(shard& s) {
            if (__atomic_load_n(&s.readers, __ATOMIC_SEQ_CST) != 0)
                return;
            for (usize i = 0; i < s.retired_count; ++i)
                s.retired[i] = {};
            s.retired_count = 0;
        }
        mm::shptr<snapshot> clone_without(const snapshot* old, const Key& key) {
            auto out = mm::shptr<snapshot>(new snapshot{});
            if (!out)
                return {};
            for (usize b = 0; b < bucket_count; ++b)
                for (const node* n = old ? old->buckets[b].get() : nullptr; n;
                     n             = n->next.get()) {
                    if (equal_(n->key, key))
                        continue;
                    auto c = mm::shptr<node>(new node(n->key, n->value, out->buckets[b]));
                    if (!c)
                        return {};
                    out->buckets[b] = static_cast<mm::shptr<node>&&>(c);
                }
            return out;
        }
        bool publish(shard& s, mm::shptr<snapshot>&& next) {
            auto old  = static_cast<mm::shptr<snapshot>&&>(s.current);
            s.current = static_cast<mm::shptr<snapshot>&&>(next);
            __atomic_store_n(&s.published, s.current.get(), __ATOMIC_SEQ_CST);
            if (old)
                s.retired[s.retired_count++] = static_cast<mm::shptr<snapshot>&&>(old);
            reclaim(s);
            return true;
        }

        public:
        class read_guard {
            const rcu_hashmap& map_;
            usize              shard_;
            snapshot*          snap_;
            shard&             state() const {
                return map_.shards_[shard_];
            }

            public:
            read_guard(const rcu_hashmap& m, usize s) : map_(m), shard_(s) {
                __atomic_add_fetch(&state().readers, 1, __ATOMIC_SEQ_CST);
                snap_ = __atomic_load_n(&state().published, __ATOMIC_SEQ_CST);
            }
            ~read_guard() {
                __atomic_sub_fetch(&state().readers, 1, __ATOMIC_SEQ_CST);
            }
            read_guard(const read_guard&) = delete;
            const Value* find(const Key& k) const {
                if (!snap_)
                    return nullptr;
                for (const node* n = snap_->buckets[map_.bucket_of(k)].get(); n;
                     n             = n->next.get())
                    if (map_.equal_(n->key, k))
                        return &n->value;
                return nullptr;
            }
        };
        rcu_hashmap(Hash h, bool (*e)(const Key&, const Key&)) : hash_(h), equal_(e) {}
        rcu_hashmap(const rcu_hashmap&) = delete;
        optional<Value> find(const Key& k) const {
            read_guard g(*this, shard_of(k));
            auto       v = g.find(k);
            return v ? optional<Value>(*v) : optional<Value>{};
        }
        bool insert_or_assign(const Key& k, const Value& v) {
            shard&               s = shards_[shard_of(k)];
            core::spinlock_guard l(s.writer);
            reclaim(s);
            if (s.current && s.readers && s.retired_count == Retired)
                return false;
            auto n = clone_without(s.current.get(), k);
            if (!n)
                return false;
            auto x = mm::shptr<node>(new node(k, v, n->buckets[bucket_of(k)]));
            if (!x)
                return false;
            n->buckets[bucket_of(k)] = static_cast<mm::shptr<node>&&>(x);
            return publish(s, static_cast<mm::shptr<snapshot>&&>(n));
        }
        bool erase(const Key& k) {
            shard&               s = shards_[shard_of(k)];
            core::spinlock_guard l(s.writer);
            reclaim(s);
            if (s.current && s.readers && s.retired_count == Retired)
                return false;
            auto n = clone_without(s.current.get(), k);
            if (!n)
                return false;
            return publish(s, static_cast<mm::shptr<snapshot>&&>(n));
        }
    };
}
