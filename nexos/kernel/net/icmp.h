/* NexOS - kernel/net/icmp.h | ICMP echo request/reply | MIT License */
#ifndef ICMP_H
#define ICMP_H

#include <stdint.h>

/* Returns RTT in ms on success, -1 on timeout */
int  icmp_send_echo(uint32_t dest_ip);
void icmp_receive(const uint8_t *ip_pkt, uint16_t ip_len,
                  const uint8_t *icmp_data, uint16_t icmp_len);

#endif
