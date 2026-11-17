#pragma once

#include <astia/module_abi_core.h>
#include <astia/module_abi_pci.h>

struct astia_module_net_packet_segment {
    const uint8_t* data;
    uint32_t length;
};

struct astia_module_net_packet_view {
    struct astia_module_net_packet_segment first;
    struct astia_module_net_packet_segment second;
    uint32_t length;
    uint32_t wire_length;
    uint64_t generation;
    void* owner;
    void (*recycle)(void* owner, int status);
};

struct astia_module_net_tx_request;
typedef void (*astia_module_net_tx_reclaim_fn)(void* context,
                                                const struct astia_module_net_tx_request* request,
                                                int status);

struct astia_module_net_tx_request {
    const uint8_t* data;
    uint64_t physical;
    uint32_t length;
    uint64_t generation;
    void* cookie;
    astia_module_net_tx_reclaim_fn reclaim;
};

enum {
    ASTIA_MODULE_NET_RX_RECYCLE = 0,
    ASTIA_MODULE_NET_RX_DEFER = 1,
    ASTIA_MODULE_NET_RX_DROP = 2,
};

typedef uint8_t (*astia_module_net_rx_irq_fn)(void* context,
                                               const struct astia_module_net_packet_view* packet);
typedef void (*astia_module_net_rx_deferred_fn)(
    void* context, const struct astia_module_net_packet_view* packet);

struct astia_module_net_device {
    const char* name;
    uint8_t mac[6];
    uint16_t mtu;
    uint32_t flags;
    uint64_t generation;
    void* context;
    astia_module_net_rx_irq_fn rx_irq;
    astia_module_net_rx_deferred_fn rx_deferred;
};

typedef int (*astia_module_pci_net_match_fn)(
    const struct astia_module_pci_device* device);
typedef int (*astia_module_pci_net_probe_fn)(const struct astia_module_pci_device* device,
                                              void** context);
typedef void (*astia_module_pci_net_remove_fn)(void* context);

struct astia_module_pci_net_driver {
    const char* name;
    astia_module_pci_net_match_fn match;
    astia_module_pci_net_probe_fn probe;
    astia_module_pci_net_remove_fn remove;
};

ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(struct astia_module_net_packet_segment) == 16,
                              "network packet segment layout");
ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(struct astia_module_net_packet_view) == 64,
                              "network packet view layout");
ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(struct astia_module_net_tx_request) == 48,
                              "network TX request layout");
ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(struct astia_module_net_device) == 56,
                              "network device layout");
ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(struct astia_module_pci_net_driver) == 32,
                              "network driver layout");

ASTIA_MODULE_ABI_EXTERN_C_BEGIN
int astia_module_register_pci_net_driver(
    const struct astia_module_pci_net_driver* driver);
int astia_module_register_net_device(const struct astia_module_net_device* device,
                                     void** handle);
int astia_module_unregister_net_device(void* handle);
ASTIA_MODULE_ABI_EXTERN_C_END
