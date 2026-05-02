/* NexOS - kernel/net/ip.c | IPv4 (RFC 791, no fragmentation) | MIT License */
#include "ip.h"
#include "ethernet.h"
#include "arp.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include "../kernel.h"
#include "../mm/heap.h"

static uint16_t ip_id = 0;

static uint16_t ip_checksum(const uint8_t *buf, int len) {
    uint32_t sum = 0;
    for (int i = 0; i + 1 < len; i += 2)
        sum += (uint32_t)((buf[i] << 8) | buf[i+1]);
    if (len & 1) sum += (uint32_t)(buf[len-1] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

int ip_send(uint32_t dest_ip, uint8_t protocol,
            const uint8_t *payload, uint16_t plen) {
    uint8_t dest_mac[6];
    /* Route: send to gateway if not on our /24 */
    uint32_t next_hop = ((dest_ip ^ eth_our_ip) & 0xFFFFFF00U)
                        ? eth_gw_ip : dest_ip;
    if (!arp_request(next_hop, dest_mac)) {
        klog(LOG_WARN, "IP: ARP failed for %d.%d.%d.%d",
             (next_hop >> 24) & 0xFF, (next_hop >> 16) & 0xFF,
             (next_hop >>  8) & 0xFF,  next_hop & 0xFF);
        return -1;
    }

    uint16_t total = (uint16_t)(IP_HDR_LEN + plen);
    uint8_t *pkt   = (uint8_t *)kmalloc(total);
    if (!pkt) return -1;

    pkt[0]  = 0x45;                          /* Version=4, IHL=5 */
    pkt[1]  = 0;
    pkt[2]  = (uint8_t)(total >> 8);
    pkt[3]  = (uint8_t)(total & 0xFF);
    pkt[4]  = (uint8_t)(ip_id >> 8);
    pkt[5]  = (uint8_t)(ip_id & 0xFF);
    ip_id++;
    pkt[6]  = 0x40;  pkt[7] = 0;            /* DF, no frag offset */
    pkt[8]  = 64;                            /* TTL */
    pkt[9]  = protocol;
    pkt[10] = 0;     pkt[11] = 0;           /* checksum placeholder */
    pkt[12] = (uint8_t)(eth_our_ip >> 24);
    pkt[13] = (uint8_t)(eth_our_ip >> 16);
    pkt[14] = (uint8_t)(eth_our_ip >>  8);
    pkt[15] = (uint8_t)(eth_our_ip);
    pkt[16] = (uint8_t)(dest_ip >> 24);
    pkt[17] = (uint8_t)(dest_ip >> 16);
    pkt[18] = (uint8_t)(dest_ip >>  8);
    pkt[19] = (uint8_t)(dest_ip);

    uint16_t cksum = ip_checksum(pkt, IP_HDR_LEN);
    pkt[10] = (uint8_t)(cksum >> 8);
    pkt[11] = (uint8_t)(cksum & 0xFF);

    for (uint16_t i = 0; i < plen; i++) pkt[IP_HDR_LEN + i] = payload[i];

    int ret = ethernet_send(dest_mac, ETH_TYPE_IP, pkt, total);
    kfree(pkt);
    return ret;
}

void ip_receive(const uint8_t *data, uint16_t len) {
    if (len < IP_HDR_LEN) return;
    uint8_t ihl      = (uint8_t)((data[0] & 0x0F) * 4);
    uint8_t protocol = data[9];
    if (len < ihl) return;

    /* Update ARP cache with sender */
    uint32_t src_ip = ((uint32_t)data[12] << 24) | ((uint32_t)data[13] << 16)
                     | ((uint32_t)data[14] <<  8) |  data[15];

    /* Verify destination is us */
    uint32_t dst_ip = ((uint32_t)data[16] << 24) | ((uint32_t)data[17] << 16)
                     | ((uint32_t)data[18] <<  8) |  data[19];
    if (dst_ip != eth_our_ip) return;

    const uint8_t *payload = data + ihl;
    uint16_t plen = (uint16_t)(len - ihl);

    switch (protocol) {
    case IP_PROTO_ICMP: icmp_receive(data, len, payload, plen);     break;
    case IP_PROTO_UDP:  udp_receive(payload, plen, src_ip);         break;
    case IP_PROTO_TCP:  tcp_receive(payload, plen, src_ip);         break;
    default:                                                         break;
    }
}
