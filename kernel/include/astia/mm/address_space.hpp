#pragma once

#include <astia/common.hpp>

namespace astia::mm {

    enum class mapping_flags : u64 {
        none          = 0,
        present       = 1ull << 0,
        writable      = 1ull << 1,
        user          = 1ull << 2,
        write_through = 1ull << 3,
        cache_disable = 1ull << 4,
        global        = 1ull << 5,
        no_execute    = 1ull << 6,
    };

    constexpr mapping_flags operator|(mapping_flags a, mapping_flags b) {
        return static_cast<mapping_flags>(static_cast<u64>(a) | static_cast<u64>(b));
    }

    constexpr bool has_flag(mapping_flags value, mapping_flags bit) {
        return (static_cast<u64>(value) & static_cast<u64>(bit)) != 0;
    }

    constexpr mapping_flags mapping_flags_from_bits(u64 bits) {
        return static_cast<mapping_flags>(bits);
    }

    enum class access : u8 {
        read,
        write,
        execute,
    };

    class address_space {
        public:
        static constexpr paddr_t invalid = ~paddr_t{0};

        address_space() = default;
        ~address_space();

        address_space(const address_space&)            = delete;
        address_space& operator=(const address_space&) = delete;
        address_space(address_space&& other) noexcept;
        address_space& operator=(address_space&& other) noexcept;

        static address_space view(void* native);
        static address_space create(const address_space* parent = nullptr);
        static address_space kernel();

        bool attached() const;
        bool valid() const;

        bool map_4k(vaddr_t virt, paddr_t phys, mapping_flags flags);
        bool unmap(vaddr_t virt);
        bool protect_4k(vaddr_t virt, mapping_flags flags);
        paddr_t translate(vaddr_t virt) const;
        bool writable(vaddr_t virt) const;
        bool user_accessible(vaddr_t virt) const;
        bool executable(vaddr_t virt) const;
        bool valid_user_range(u64 address, usize length) const;
        bool validate(u64 address, usize length, access required) const;

        void activate() const;
        static void invalidate(vaddr_t virt);

        private:
        explicit address_space(void* native, bool owned)
            : native_(native), owned_(owned) {}

        void* native_ = nullptr;
        bool  owned_  = false;
    };

    class mapping_transaction {
        public:
        mapping_transaction(address_space& space, vaddr_t first, usize pages);
        ~mapping_transaction();

        mapping_transaction(const mapping_transaction&)            = delete;
        mapping_transaction& operator=(const mapping_transaction&) = delete;

        bool map_4k(vaddr_t virt, paddr_t phys, mapping_flags flags);
        void commit();
        void rollback();

        private:
        address_space* space_   = nullptr;
        vaddr_t        first_   = 0;
        usize          pages_   = 0;
        usize          mapped_  = 0;
        bool           finished_ = false;
    };

} // namespace astia::mm
