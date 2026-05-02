/* NexOS - kernel/net/arp.c | ARP (RFC 826) | MIT License */
#include "arp.h"
#include "ethernet.h"
#include "../kernel.h"
#include "../drivers/rtl8139.h"
#include "../drivers/timer.h"

/* ── ARP table ───────────────────────────────────────────────────────────── */
typedef struct { uint32_t ip; uint8_t mac[6]; int valid; } arp_entry_t;
static arp_entry_t arp_table[ARP_TABLE_SIZE];

static void mcopy(void *d, const void *s, int n) {
    uint8_t *dd = (uint8_t *)d;
    const uint8_t *ss = (const uint8_t *)s;
    for (int i = 0; i < n; i++) dd[i] = ss[i];
}

void arp_cache_set(uint32_t ip, const uint8_t mac[6]) {
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (!arp_table[i].valid || arp_table[i].ip == ip) {
            arp_table[i].ip    = ip;
            arp_table[i].valid = 1;
            mcopy(arp_table[i].mac, mac, 6);
            return;
        }
    }
}

int arp_lookup(uint32_t ip, uint8_t mac_out[6]) {
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (arp_table[i].valid && arp_table[i].ip == ip) {
            mcopy(mac_out, arp_table[i].mac, 6);
            return 1;
        }
    }
    return 0;
}

/* ── ARP packet builder helpers ──────────────────────────────────────────── */
static void build_arp_pkt(uint8_t *pkt, uint16_t op,
                           const uint8_t *send_mac, uint32_t send_ip,
                           const uint8_t *tgt_mac,  uint32_t tgt_ip) {
    pkt[0] = 0x00; pkt[1] = 0x01;               /* HW type: Ethernet */
    pkt[2] = 0x08; pkt[3] = 0x00;               /* Proto: IPv4       */
    pkt[4] = 6;    pkt[5] = 4;                  /* HW/proto len      */
    pkt[6] = (uint8_t)(op >> 8);
    pkt[7] = (uint8_t)(op & 0xFF);
    mcopy(pkt + 8,  send_mac, 6);               /* sender MAC        */
    pkt[14] = (uint8_t)(send_ip >> 24);
    pkt[15] = (uint8_t)(send_ip >> 16);
    pkt[16] = (uint8_t)(send_ip >>  8);
    pkt[17] = (uint8_t)(send_ip);               /* sender IP         */
    for (int i = 0; i < 6; i++) pkt[18 + i] = tgt_mac ? tgt_mac[i] : 0;
    pkt[24] = (uint8_t)(tgt_ip >> 24);
    pkt[25] = (uint8_t)(tgt_ip >> 16);
    pkt[26] = (uint8_t)(tgt_ip >>  8);
    pkt[27] = (uint8_t)(tgt_ip);               /* target IP         */
}

/* ── arp_request: send broadcast request, poll up to ARP_TIMEOUT_MS ─────── */
int arp_request(uint32_t target_ip, uint8_t mac_out[6]) {
    if (arp_lookup(target_ip, mac_out)) return 1;   /* cache hit */

    uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    uint8_t zeros[6] = {0};
    uint8_t pkt[28];
    build_arp_pkt(pkt, 1, eth_our_mac, eth_our_ip, zeros, target_ip);
    ethernet_send(bcast, ETH_TYPE_ARP, pkt, 28);

    uint64_t deadline = timer_get_ticks() + ARP_TIMEOUT_MS;
    while (timer_get_ticks() < deadline) {
        rtl8139_receive();
        if (arp_lookup(target_ip, mac_out)) return 1;
    }
    return 0;
}

/* ── arp_reply: send reply to an ARP request frame ──────────────────────── */
void arp_reply(const uint8_t *frame) {
    const uint8_t *arp = frame + ETH_HDR_LEN;   /* ARP payload at offset 14 */
    uint32_t requester_ip = ((uint32_t)arp[14] << 24) | ((uint32_t)arp[15] << 16)
                           | ((uint32_t)arp[16] << 8)  |  arp[17];
    uint8_t pkt[28];
    build_arp_pkt(pkt, 2, eth_our_mac, eth_our_ip, arp + 8, requester_ip);
    ethernet_send(arp + 8, ETH_TYPE_ARP, pkt, 28);
    klog(LOG_INFO, "ARP: reply sent to %d.%d.%d.%d",
         arp[14], arp[15], arp[16], arp[17]);
}

/* ── arp_receive: dispatch incoming ARP frames ───────────────────────────── */
void arp_receive(const uint8_t *frame, uint16_t len) {
    if (len < ETH_HDR_LEN + 28) return;
    const uint8_t *arp = frame + ETH_HDR_LEN;

    /* Only handle Ethernet + IPv4 */
    if (arp[0] != 0x00 || arp[1] != 0x01) return;
    if (arp[2] != 0x08 || arp[3] != 0x00) return;

    uint16_t op = (uint16_t)((arp[6] << 8) | arp[7]);

    /* Always update the cache with the sender's info */
    uint32_t sender_ip = ((uint32_t)arp[14] << 24) | ((uint32_t)arp[15] << 16)
                        | ((uint32_t)arp[16] << 8)  |  arp[17];
    arp_cache_set(sender_ip, arp + 8);

    if (op == 1) {
        /* ARP request — check if it's for our IP */
        uint32_t tgt_ip = ((uint32_t)arp[24] << 24) | ((uint32_t)arp[25] << 16)
                         | ((uint32_t)arp[26] << 8)  |  arp[27];
        if (tgt_ip == eth_our_ip)
            arp_reply(frame);
    }
    /* op == 2 (reply): cache already updated above */
}
