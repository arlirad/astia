#include <astia/module.hpp>

#include "module.hpp"

// The module-face lifetime lease. The registry already owns active-reference
// accounting; a token is one lease reference plus one reference per guarded
// callback entry, so the image cannot be unmapped while either is live.

namespace astia::module {
    namespace {
        astia::mod::module_id token_id(u32 slot, u64 generation) {
            return astia::mod::module_id{slot, generation};
        }
    } // namespace

    lifetime_token::~lifetime_token() {
        release();
    }

    lifetime_token::lifetime_token(lifetime_token&& other) noexcept
        : _slot(other._slot), _generation(other._generation),
          _released(__atomic_load_n(&other._released, __ATOMIC_ACQUIRE)) {
        other._slot       = no_slot;
        other._generation = 0;

        __atomic_store_n(&other._released, true, __ATOMIC_RELEASE);
    }

    lifetime_token& lifetime_token::operator=(lifetime_token&& other) noexcept {
        if (this == &other)
            return *this;

        release();

        _slot       = other._slot;
        _generation = other._generation;

        __atomic_store_n(&_released, __atomic_load_n(&other._released, __ATOMIC_ACQUIRE),
                         __ATOMIC_RELEASE);

        other._slot       = no_slot;
        other._generation = 0;

        __atomic_store_n(&other._released, true, __ATOMIC_RELEASE);

        return *this;
    }

    lifetime_token lifetime_token::acquire() {
        const astia::mod::module_id id = astia::mod::active_module_id();

        if (!id.valid())
            return {};

        if (astia::mod::acquire(id) != astia::mod::module_error::ok)
            return {};

        return lifetime_token(id.slot, id.generation);
    }

    bool lifetime_token::valid() const {
        return _slot != no_slot && !__atomic_load_n(&_released, __ATOMIC_ACQUIRE);
    }

    void lifetime_token::release() {
        if (_slot == no_slot)
            return;

        if (__atomic_exchange_n(&_released, true, __ATOMIC_ACQ_REL))
            return;

        (void) astia::mod::release(token_id(_slot, _generation));
    }

    bool lifetime_token::make_resident() const {
        if (!valid())
            return false;

        return astia::mod::make_resident(token_id(_slot, _generation)) ==
               astia::mod::module_error::ok;
    }

    bool lifetime_token::try_enter() const {
        if (!valid())
            return false;

        return astia::mod::acquire(token_id(_slot, _generation)) ==
               astia::mod::module_error::ok;
    }

    void lifetime_token::leave() const {
        if (_slot == no_slot)
            return;

        (void) astia::mod::release(token_id(_slot, _generation));
    }
} // namespace astia::module
