/* NexOS - kernel/net/udp.c | UDP (RFC 768) | MIT License */
#include "udp.h"
#include "ip.h"
#include "../kernel.h"
#include "../mm/heap.h"

#define UDP_MAX_HANDLERS 8

typedef struct {
    uint16_t       port;
    udp_handler_fn fn;
} udp_slot_t;

static udp_slot_t udp_handlers[UDP_MAX_HANDLERS];

/* ── registration ─────────────────────────────────────────────────────────── */
void udp_register(uint16_t port, udp_handler_fn fn) {
    for (int i = 0; i < UDP_MAX_HANDLERS; i++) {
        if (!udp_handlers[i].fn || udp_handlers[i].port == port) {
            udp_handlers[i].port = port;
            udp_handlers[i].fn   = fn;
            return;
        }
    }
    klog(LOG_WARN, "UDP: handler table full, port %d dropped", (int)port);
}

void udp_unregister(uint16_t port) {
    for (int i = 0; i < UDP_MAX_HANDLERS; i++) {
        if (udp_handlers[i].fn && udp_handlers[i].port == port) {
            udp_handlers[i].fn   = 0;
            udp_handlers[i].port = 0;
            return;
        }
    }
}

/* ── send ─────────────────────────────────────────────────────────────────── */
int udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
             const uint8_t *data, uint16_t len) {
    uint16_t total = (uint16_t)(UDP_HDR_LEN + len);
    uint8_t *pkt   = (uint8_t *)kmalloc(total);
    if (!pkt) return -1;

    pkt[0] = (uint8_t)(src_port >> 8);
    pkt[1] = (uint8_t)(src_port & 0xFF);
    pkt[2] = (uint8_t)(dst_port >> 8);
    pkt[3] = (uint8_t)(dst_port & 0xFF);
    pkt[4] = (uint8_t)(total >> 8);
    pkt[5] = (uint8_t)(total & 0xFF);
    pkt[6] = 0; pkt[7] = 0;  /* checksum optional for IPv4 */
    for (uint16_t i = 0; i < len; i++) pkt[UDP_HDR_LEN + i] = data[i];

    int ret = ip_send(dst_ip, 17, pkt, total);
    kfree(pkt);
    return ret;
}

/* ── receive ─────────────────────────────────────────────────────────────── */
void udp_receive(const uint8_t *data, uint16_t len, uint32_t src_ip) {
    if (len < UDP_HDR_LEN) return;
    uint16_t src_port = (uint16_t)((data[0] << 8) | data[1]);
    uint16_t dst_port = (uint16_t)((data[2] << 8) | data[3]);
    uint16_t udp_len  = (uint16_t)((data[4] << 8) | data[5]);
    if (udp_len < UDP_HDR_LEN || udp_len > len) return;

    const uint8_t *payload = data + UDP_HDR_LEN;
    uint16_t       plen    = (uint16_t)(udp_len - UDP_HDR_LEN);

    for (int i = 0; i < UDP_MAX_HANDLERS; i++) {
        if (udp_handlers[i].fn && udp_handlers[i].port == dst_port) {
            udp_handlers[i].fn(src_ip, src_port, payload, plen);
            return;
        }
    }
}
