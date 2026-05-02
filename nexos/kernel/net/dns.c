/* NexOS - kernel/net/dns.c | DNS A-record resolver | MIT License */
#include "dns.h"
#include "udp.h"
#include "../kernel.h"
#include "../drivers/rtl8139.h"
#include "../drivers/timer.h"

#define DNS_SERVER    0x0A000203U   /* 10.0.2.3 (QEMU NAT DNS) */
#define DNS_PORT      53
#define DNS_SRC_PORT  1053
#define DNS_TIMEOUT   3000          /* ms */

static volatile int dns_got_reply = 0;
static uint8_t      dns_result[4];
static uint16_t     dns_txid = 0;

/* ── helpers ─────────────────────────────────────────────────────────────── */
static int dns_skip_name(const uint8_t *data, int off, int len) {
    while (off < len) {
        uint8_t c = data[off];
        if (c == 0)            return off + 1;
        if ((c & 0xC0) == 0xC0) return off + 2;   /* pointer */
        off += c + 1;
    }
    return off;
}

static int dns_encode_name(const char *host, uint8_t *buf) {
    int pos = 0;
    while (*host) {
        const char *dot = host;
        while (*dot && *dot != '.') dot++;
        int label_len = (int)(dot - host);
        buf[pos++] = (uint8_t)label_len;
        for (int i = 0; i < label_len; i++) buf[pos++] = (uint8_t)host[i];
        host = dot;
        if (*host == '.') host++;
    }
    buf[pos++] = 0;   /* root label */
    return pos;
}

/* ── UDP callback for port DNS_SRC_PORT ─────────────────────────────────── */
static void dns_udp_handler(uint32_t src_ip, uint16_t src_port,
                             const uint8_t *data, uint16_t len) {
    (void)src_ip; (void)src_port;
    if (len < 12) return;
    uint16_t txid = (uint16_t)((data[0] << 8) | data[1]);
    if (txid != dns_txid) return;
    if (!(data[2] & 0x80)) return;                /* not a response */
    uint16_t qdcount = (uint16_t)((data[4] << 8) | data[5]);
    uint16_t ancount = (uint16_t)((data[6] << 8) | data[7]);
    if (ancount == 0) return;

    int pos = 12;
    /* skip question section */
    for (int q = 0; q < qdcount && pos < len; q++) {
        pos = dns_skip_name(data, pos, len);
        pos += 4;   /* type + class */
    }
    /* parse answer records */
    for (int a = 0; a < ancount && pos < len; a++) {
        pos = dns_skip_name(data, pos, len);
        if (pos + 10 > len) break;
        uint16_t type  = (uint16_t)((data[pos] << 8)   | data[pos+1]);
        uint16_t rdlen = (uint16_t)((data[pos+8] << 8) | data[pos+9]);
        pos += 10;
        if (type == 1 && rdlen == 4 && pos + 4 <= len) {
            dns_result[0] = data[pos];
            dns_result[1] = data[pos+1];
            dns_result[2] = data[pos+2];
            dns_result[3] = data[pos+3];
            dns_got_reply = 1;
            return;
        }
        pos += rdlen;
    }
}

/* ── public API ──────────────────────────────────────────────────────────── */
int dns_resolve(const char *hostname, uint8_t ip_out[4]) {
    uint8_t pkt[512];
    int pos = 0;

    dns_txid = (uint16_t)((timer_get_ticks() ^ 0xA5A5) & 0xFFFF);
    if (!dns_txid) dns_txid = 0x1234;

    /* DNS header */
    pkt[pos++] = (uint8_t)(dns_txid >> 8);
    pkt[pos++] = (uint8_t)(dns_txid & 0xFF);
    pkt[pos++] = 0x01; pkt[pos++] = 0x00;  /* flags: RD=1 */
    pkt[pos++] = 0x00; pkt[pos++] = 0x01;  /* qdcount=1 */
    pkt[pos++] = 0x00; pkt[pos++] = 0x00;  /* ancount=0 */
    pkt[pos++] = 0x00; pkt[pos++] = 0x00;  /* nscount=0 */
    pkt[pos++] = 0x00; pkt[pos++] = 0x00;  /* arcount=0 */

    pos += dns_encode_name(hostname, pkt + pos);
    pkt[pos++] = 0x00; pkt[pos++] = 0x01;  /* type A */
    pkt[pos++] = 0x00; pkt[pos++] = 0x01;  /* class IN */

    dns_got_reply = 0;
    udp_register(DNS_SRC_PORT, dns_udp_handler);

    klog(LOG_INFO, "DNS: querying %s ...", hostname);
    if (udp_send(DNS_SERVER, DNS_SRC_PORT, DNS_PORT,
                 pkt, (uint16_t)pos) < 0) {
        udp_unregister(DNS_SRC_PORT);
        klog(LOG_WARN, "DNS: send failed");
        return -1;
    }

    uint64_t deadline = timer_get_ticks() + DNS_TIMEOUT;
    while (timer_get_ticks() < deadline) {
        rtl8139_receive();
        if (dns_got_reply) {
            ip_out[0] = dns_result[0];
            ip_out[1] = dns_result[1];
            ip_out[2] = dns_result[2];
            ip_out[3] = dns_result[3];
            udp_unregister(DNS_SRC_PORT);
            klog(LOG_INFO, "DNS: %s -> %d.%d.%d.%d", hostname,
                 ip_out[0], ip_out[1], ip_out[2], ip_out[3]);
            return 0;
        }
    }

    udp_unregister(DNS_SRC_PORT);
    klog(LOG_WARN, "DNS: timeout for %s", hostname);
    return -1;
}
