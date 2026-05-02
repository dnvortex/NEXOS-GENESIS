/* NexOS - kernel/net/arp.h | ARP layer | MIT License */
#ifndef ARP_H
#define ARP_H

#include <stdint.h>

#define ARP_TABLE_SIZE  16
#define ARP_TIMEOUT_MS  1000

void arp_receive(const uint8_t *frame, uint16_t len);
int  arp_request(uint32_t target_ip, uint8_t mac_out[6]);
void arp_reply(const uint8_t *frame);
int  arp_lookup(uint32_t ip, uint8_t mac_out[6]);
void arp_cache_set(uint32_t ip, const uint8_t mac[6]);

#endif
