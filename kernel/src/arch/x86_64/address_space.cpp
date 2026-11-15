#include <astia/arch/layout.hpp>
#include <astia/arch/page_map.hpp>
#include <astia/arch/user_layout.hpp>
#include <astia/mm/address_space.hpp>

namespace astia::arch {
    page_map* kernel_page_map() __attribute__((weak));
}

namespace astia::mm {

    namespace {
        arch::page_flags arch_flags(mapping_flags flags) {
            return static_cast<arch::page_flags>(static_cast<u64>(flags));
        }
    } // namespace

    address_space::~address_space() {
        if (owned_)
            delete static_cast<arch::page_map*>(native_);
    }

    address_space::address_space(address_space&& other) noexcept
        : native_(other.native_), owned_(other.owned_) {
        other.native_ = nullptr;
        other.owned_  = false;
    }

    address_space& address_space::operator=(address_space&& other) noexcept {
        if (this != &other) {
            if (owned_)
                delete static_cast<arch::page_map*>(native_);
            native_       = other.native_;
            owned_        = other.owned_;
            other.native_ = nullptr;
            other.owned_  = false;
        }
        return *this;
    }

    address_space address_space::view(void* native) {
        return address_space(native, false);
    }

    address_space address_space::create(const address_space* parent) {
        const auto* parent_map = parent == nullptr
                                     ? arch::kernel_page_map()
                                     : static_cast<const arch::page_map*>(parent->native_);
        auto* map = new arch::page_map(arch::page_map::create(parent_map));
        if (map == nullptr || !map->valid()) {
            delete map;
            return {};
        }
        return address_space(map, true);
    }

    address_space address_space::kernel() {
        return address_space(arch::kernel_page_map(), false);
    }

    bool address_space::attached() const {
        return native_ != nullptr;
    }

    bool address_space::valid() const {
        return attached() && static_cast<const arch::page_map*>(native_)->valid();
    }

    bool address_space::map_4k(vaddr_t virt, paddr_t phys, mapping_flags flags) {
        return valid() && static_cast<arch::page_map*>(native_)->map_4k(
                               virt, phys, arch_flags(flags));
    }

    bool address_space::unmap(vaddr_t virt) {
        return valid() && static_cast<arch::page_map*>(native_)->unmap(virt);
    }

    bool address_space::protect_4k(vaddr_t virt, mapping_flags flags) {
        return valid() && static_cast<arch::page_map*>(native_)->protect_4k(
                               virt, arch_flags(flags));
    }

    paddr_t address_space::translate(vaddr_t virt) const {
        return valid() ? static_cast<const arch::page_map*>(native_)->translate(virt)
                       : invalid;
    }

    bool address_space::writable(vaddr_t virt) const {
        return valid() && static_cast<const arch::page_map*>(native_)->writable(virt);
    }

    bool address_space::user_accessible(vaddr_t virt) const {
        return valid() &&
               static_cast<const arch::page_map*>(native_)->user_accessible(virt);
    }

    bool address_space::executable(vaddr_t virt) const {
        return valid() && static_cast<const arch::page_map*>(native_)->executable(virt);
    }

    bool address_space::valid_user_range(u64 address, usize length) const {
        if (length == 0)
            return true;
        if (address == 0 ||
            !arch::user_layout_policy::is_canonical(address) ||
            address >= arch::user_layout_policy::address_limit)
            return false;
        const u64 last = address + static_cast<u64>(length) - 1;
        if (last < address || last >= arch::user_layout_policy::address_limit)
            return false;
        return arch::user_layout_policy::is_canonical(last);
    }

    bool address_space::validate(u64 address, usize length, access required) const {
        if (length == 0)
            return true;
        if (!valid_user_range(address, length))
            return false;
        const u64 first = address & ~page_mask;
        const u64 last  = (address + length - 1) & ~page_mask;
        for (u64 page = first;; page += page_size) {
            if (!user_accessible(page))
                return false;
            if ((required == access::write && !writable(page)) ||
                (required == access::execute && !executable(page)))
                return false;
            if (page == last)
                return true;
        }
    }

    void address_space::activate() const {
        if (valid())
            static_cast<const arch::page_map*>(native_)->activate();
    }

    void address_space::invalidate(vaddr_t virt) {
        arch::page_map::invalidate(virt);
    }

    mapping_transaction::mapping_transaction(address_space& space, vaddr_t first,
                                             usize pages)
        : space_(&space), first_(first), pages_(pages) {}

    mapping_transaction::~mapping_transaction() {
        if (!finished_)
            rollback();
    }

    bool mapping_transaction::map_4k(vaddr_t virt, paddr_t phys, mapping_flags flags) {
        if (finished_ || space_ == nullptr || mapped_ >= pages_ ||
            virt != first_ + mapped_ * page_size ||
            first_ + mapped_ * page_size < first_)
            return false;
        if (space_->translate(virt) != address_space::invalid)
            return false;
        if (!space_->map_4k(virt, phys, flags))
            return false;
        ++mapped_;
        return true;
    }

    void mapping_transaction::commit() {
        finished_ = true;
    }

    void mapping_transaction::rollback() {
        if (finished_ || space_ == nullptr)
            return;
        for (usize page = 0; page < mapped_; ++page) {
            const vaddr_t virt = first_ + page * page_size;
            (void) space_->unmap(virt);
            address_space::invalidate(virt);
        }
        finished_ = true;
        mapped_   = 0;
    }

} // namespace astia::mm
