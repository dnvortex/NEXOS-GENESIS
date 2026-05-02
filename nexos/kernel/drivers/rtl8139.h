/* NexOS — kernel/drivers/rtl8139.h | RTL8139 NIC driver | MIT License */
#ifndef RTL8139_H
#define RTL8139_H

#include <stdint.h>

/* Our static IP in QEMU user networking (NAT) */
#define RTL_OUR_IP   { 10, 0, 2, 15 }
#define RTL_GW_IP    { 10, 0, 2,  2 }

int  rtl8139_init(void);
int  rtl8139_found(void);
void rtl8139_get_mac(uint8_t mac[6]);
int  rtl8139_send(const uint8_t *data, uint16_t len);
void rtl8139_receive(void);          /* poll & drain RX ring */
void rtl8139_set_rx_callback(void (*cb)(const uint8_t *pkt, uint16_t len));
void rtl8139_arp_reply(const uint8_t *req_pkt);
uint32_t rtl8139_get_rx_count(void);
uint32_t rtl8139_get_tx_count(void);

#endif
