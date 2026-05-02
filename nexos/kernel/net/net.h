/* NexOS — kernel/net/net.h | Network subsystem stubs | MIT License */
#ifndef NET_H
#define NET_H

#include <stdint.h>

#define NET_HWADDR_LEN 6

struct net_interface {
    char     name[16];
    uint8_t  hwaddr[NET_HWADDR_LEN];
    uint32_t ip4_addr;
    uint32_t ip4_mask;
    uint32_t ip4_gw;
    int      flags;
};

struct ip_packet {
    uint8_t  version_ihl;
    uint8_t  dscp_ecn;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_addr;
    uint32_t dst_addr;
    uint8_t  payload[];
} __attribute__((packed));

void net_init(void);

#endif
