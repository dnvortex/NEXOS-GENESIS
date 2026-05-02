/* NexOS - kernel/net/udp.h | UDP layer | MIT License */
#ifndef UDP_H
#define UDP_H

#include <stdint.h>

#define UDP_HDR_LEN 8

typedef void (*udp_handler_fn)(uint32_t src_ip, uint16_t src_port,
                                const uint8_t *data, uint16_t len);

int  udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
              const uint8_t *data, uint16_t len);
void udp_receive(const uint8_t *data, uint16_t len, uint32_t src_ip);
void udp_register(uint16_t port, udp_handler_fn fn);
void udp_unregister(uint16_t port);

#endif
