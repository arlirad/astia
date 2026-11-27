#pragma once

#include <astia/mm/dma_buffer.hpp>
#include <astia/net.hpp>
#include <astia/net/link.hpp>
#include <astia/net/netdev.hpp>

#include "../types/mac.hpp"

namespace astia::eth {
    /// Ethernet II implementation of net::link: framing, minimum-size padding,
    /// EtherType demux, and the canonical protocol translation.
    class link final : public net::link {
        public:
        ~link() override {
            detach();
        }

        /// Binds the link to a device.  The link keeps only a weak reference;
        /// the bus/registry owns the strong reference for the binding lifetime.
        bool attach(net::netdev& device, const mm::shptr<net::netdev>& reference);
        void detach();

        net::link_kind           kind() const override;
        net::hardware_type       hardware() const override;
        const net::link_address& local_address() const override;

        core::task<void> send(const net::link_address& dst, net::protocol_id protocol,
                              span<const u8> payload, core::cltoken token) override;

        void rx(net::packet_view packet) override;

        private:
        static constexpr u32 max_frame = 1514; // 14-byte header + 1500
        static constexpr u32 min_frame = 60;   // without FCS
        static constexpr u32 tx_slots  = 4;

        struct tx_slot {
            mm::dma_buffer buffer{};
            link*          owner = nullptr;
            bool           used  = false;
        };

        tx_slot* acquire_slot();
        void     release_slot(tx_slot& slot);

        static void reclaim(void* owner, const net::tx_request* request, int status);

        mm::shweak<net::netdev> _dev{};
        mac                     _mac{};
        net::link_address       _address{};
        bool                    _bound = false;
        tx_slot                 _tx[tx_slots]{};
    };
} // namespace astia::eth
