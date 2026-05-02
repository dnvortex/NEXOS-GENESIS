/* NexOS - kernel/net/icmp.c | ICMP echo request & reply | MIT License */
#include "icmp.h"
#include "ip.h"
#include "../kernel.h"
#include "../drivers/rtl8139.h"
#include "../drivers/timer.h"

#define ICMP_ECHO_REQUEST  8
#define ICMP_ECHO_REPLY    0
#define ICMP_HDR_LEN       8
#define ICMP_DATA_LEN      32
#define PING_TIMEOUT_MS  1000

static uint16_t icmp_id  = 0x4E58;  /* 'NX' */
static uint16_t icmp_seq = 0;

static volatile int      icmp_got_reply = 0;
static volatile uint16_t icmp_reply_seq = 0;

static uint16_t icmp_checksum(const uint8_t *data, int len) {
    uint32_t sum = 0;
    for (int i = 0; i + 1 < len; i += 2)
        sum += (uint32_t)((data[i] << 8) | data[i+1]);
    if (len & 1) sum += (uint32_t)(data[len-1] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

/* ── icmp_send_echo ──────────────────────────────────────────────────────── */
int icmp_send_echo(uint32_t dest_ip) {
    uint16_t seq = ++icmp_seq;
    icmp_got_reply = 0;

    uint8_t pkt[ICMP_HDR_LEN + ICMP_DATA_LEN];
    pkt[0] = ICMP_ECHO_REQUEST;
    pkt[1] = 0;
    pkt[2] = 0; pkt[3] = 0;                         /* checksum placeholder */
    pkt[4] = (uint8_t)(icmp_id >> 8);
    pkt[5] = (uint8_t)(icmp_id & 0xFF);
    pkt[6] = (uint8_t)(seq >> 8);
    pkt[7] = (uint8_t)(seq & 0xFF);
    for (int i = 0; i < ICMP_DATA_LEN; i++)
        pkt[ICMP_HDR_LEN + i] = (uint8_t)i;

    uint16_t ck = icmp_checksum(pkt, ICMP_HDR_LEN + ICMP_DATA_LEN);
    pkt[2] = (uint8_t)(ck >> 8);
    pkt[3] = (uint8_t)(ck & 0xFF);

    uint64_t t0 = timer_get_ticks();
    if (ip_send(dest_ip, IP_PROTO_ICMP, pkt, ICMP_HDR_LEN + ICMP_DATA_LEN) < 0)
        return -1;

    uint64_t deadline = t0 + PING_TIMEOUT_MS;
    while (timer_get_ticks() < deadline) {
        rtl8139_receive();
        if (icmp_got_reply && icmp_reply_seq == seq)
            return (int)(timer_get_ticks() - t0);
    }
    return -1;
}

/* ── icmp_receive ────────────────────────────────────────────────────────── */
void icmp_receive(const uint8_t *ip_pkt, uint16_t ip_len,
                  const uint8_t *data,   uint16_t len) {
    if (len < ICMP_HDR_LEN) return;

    uint8_t  type     = data[0];
    uint16_t their_id = (uint16_t)((data[4] << 8) | data[5]);
    uint16_t their_seq= (uint16_t)((data[6] << 8) | data[7]);

    if (type == ICMP_ECHO_REPLY) {
        if (their_id == icmp_id) {
            icmp_reply_seq = their_seq;
            icmp_got_reply = 1;
        }
        return;
    }

    if (type == ICMP_ECHO_REQUEST) {
        /* Get requester IP from IP header */
        uint32_t src_ip = ((uint32_t)ip_pkt[12] << 24)
                        | ((uint32_t)ip_pkt[13] << 16)
                        | ((uint32_t)ip_pkt[14] <<  8)
                        |  ip_pkt[15];
        (void)ip_len;

        /* Mirror the request back as a reply */
        uint8_t reply[ICMP_HDR_LEN + ICMP_DATA_LEN];
        uint16_t rlen = (len > ICMP_HDR_LEN + ICMP_DATA_LEN)
                        ? ICMP_HDR_LEN + ICMP_DATA_LEN : len;
        for (uint16_t i = 0; i < rlen; i++) reply[i] = data[i];
        reply[0] = ICMP_ECHO_REPLY;
        reply[2] = 0; reply[3] = 0;
        uint16_t ck = icmp_checksum(reply, rlen);
        reply[2] = (uint8_t)(ck >> 8);
        reply[3] = (uint8_t)(ck & 0xFF);

        ip_send(src_ip, IP_PROTO_ICMP, reply, rlen);
        klog(LOG_INFO, "ICMP: echo reply to %d.%d.%d.%d seq=%d",
             (src_ip>>24)&0xFF, (src_ip>>16)&0xFF,
             (src_ip>> 8)&0xFF,  src_ip&0xFF, their_seq);
    }
}
