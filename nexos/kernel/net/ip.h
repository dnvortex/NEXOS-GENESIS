/* NexOS - kernel/net/ip.h | IPv4 layer | MIT License */
#ifndef IP_H
#define IP_H

#include <stdint.h>

#define IP_PROTO_ICMP  1
#define IP_PROTO_TCP   6
#define IP_PROTO_UDP   17
#define IP_HDR_LEN     20

typedef struct {
    uint8_t  version_ihl;
    uint8_t  dscp_ecn;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src;
    uint32_t dst;
} __attribute__((packed)) ip_header_t;

int  ip_send(uint32_t dest_ip, uint8_t protocol,
             const uint8_t *payload, uint16_t len);
void ip_receive(const uint8_t *data, uint16_t len);

#endif
