/* NexOS - kernel/net/ethernet.c | Ethernet II frame layer | MIT License */
#include "ethernet.h"
#include "arp.h"
#include "ip.h"
#include "../kernel.h"
#include "../drivers/rtl8139.h"
#include "../mm/heap.h"

uint8_t  eth_our_mac[ETH_ADDR_LEN] = {0};
uint32_t eth_our_ip  = 0x0A00020F;   /* 10.0.2.15 */
uint32_t eth_gw_ip   = 0x0A000202;   /* 10.0.2.2  */

void ethernet_init(void) {
    rtl8139_get_mac(eth_our_mac);
    rtl8139_set_rx_callback(ethernet_receive);
    klog(LOG_INFO,
         "Ethernet: MAC=%02x:%02x:%02x:%02x:%02x:%02x IP=10.0.2.15 GW=10.0.2.2",
         eth_our_mac[0], eth_our_mac[1], eth_our_mac[2],
         eth_our_mac[3], eth_our_mac[4], eth_our_mac[5]);
}

int ethernet_send(const uint8_t dst_mac[ETH_ADDR_LEN], uint16_t ethertype,
                  const uint8_t *payload, uint16_t plen) {
    uint16_t total = (uint16_t)(ETH_HDR_LEN + plen);
    uint8_t *frame = (uint8_t *)kmalloc(total);
    if (!frame) return -1;

    /* Destination and source MAC */
    for (int i = 0; i < ETH_ADDR_LEN; i++) frame[i]          = dst_mac[i];
    for (int i = 0; i < ETH_ADDR_LEN; i++) frame[6 + i]      = eth_our_mac[i];
    /* EtherType big-endian */
    frame[12] = (uint8_t)(ethertype >> 8);
    frame[13] = (uint8_t)(ethertype & 0xFF);
    /* Payload */
    for (uint16_t i = 0; i < plen; i++) frame[ETH_HDR_LEN + i] = payload[i];

    int ret = rtl8139_send(frame, total);
    kfree(frame);
    return ret;
}

void ethernet_receive(const uint8_t *frame, uint16_t len) {
    if (len < ETH_HDR_LEN) return;

    uint16_t ethertype = (uint16_t)((frame[12] << 8) | frame[13]);
    const uint8_t *payload = frame + ETH_HDR_LEN;
    uint16_t plen = len - ETH_HDR_LEN;

    switch (ethertype) {
    case ETH_TYPE_ARP: arp_receive(frame, len);       break;
    case ETH_TYPE_IP:  ip_receive(payload, plen);     break;
    default:                                           break;
    }
}
