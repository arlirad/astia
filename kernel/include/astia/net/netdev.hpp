#pragma once

#include <astia/net.hpp>

namespace astia::net {
    class netdev final {
        public:
        static constexpr u32 tx_pending_depth    = 16;
        static constexpr u32 tx_completion_depth = 4;

        netdev(const char* name, void* context = nullptr) : _tx(context) {
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

            _tx.initialize(context);

            _address = {};
            _kind    = link_kind::unspecified;
            _link    = {};
        }

        bool valid() const {
            return _name[0] != '\0';
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
        link_address                                   _address{};
        link_kind                                      _kind = link_kind::unspecified;
        mm::shptr<link>                                _link{};
        u16                                            _mtu        = default_mtu;
        u32                                            _link_flags = 0;
        tx_path<tx_pending_depth, tx_completion_depth> _tx;
    };

    using net_device = netdev;
} // namespace astia::net
