#pragma once

#include <astia/common.hpp>

namespace astia::dl::detail {
    template <typename ImagePointer>
    struct binding {
        ImagePointer img;
        u64          base    = 0;
        u32          handles = 0;
    };

    template <typename Owner, typename Binding, u32 MaxBindings>
    struct row {
        Owner*  owner = nullptr;
        u32     count = 0;
        Binding items[MaxBindings]{};
    };

    /// Fixed registry rows keyed by owner.  `adopt` republishes the staging
    /// row in place, so a full table can never strand the staged bindings.
    template <typename Owner, typename Binding, u32 MaxRows, u32 MaxBindings>
    class row_table {
        public:
        using row_type = row<Owner, Binding, MaxBindings>;

        row_type* find(Owner* owner) {
            for (auto& value : _rows) {
                if (value.owner == owner)
                    return &value;
            }

            return nullptr;
        }

        row_type* make(Owner* owner) {
            for (auto& value : _rows) {
                if (value.owner == nullptr) {
                    value.owner = owner;

                    return &value;
                }
            }

            return nullptr;
        }

        void clear(row_type& value) {
            value.owner = nullptr;
            value.count = 0;

            for (auto& item : value.items) {
                item = {};
            }
        }

        template <typename Release>
        bool adopt(Owner* target, Owner* staging, Release release) {
            auto* source = find(staging);

            if (source == nullptr || source->count == 0)
                return true;

            auto* destination = find(target);

            if (destination != nullptr && destination->count != 0) {
                release(*source);
                clear(*source);

                return false;
            }

            if (destination == nullptr) {
                source->owner = target;

                return true;
            }

            *destination       = *source;
            destination->owner = target;

            clear(*source);

            return true;
        }

        private:
        row_type _rows[MaxRows]{};
    };
} // namespace astia::dl::detail
