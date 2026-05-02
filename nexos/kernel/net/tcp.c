/* NexOS - kernel/net/tcp.c | Minimal TCP state machine (polling) | MIT License */
#include "tcp.h"
#include "ip.h"
#include "ethernet.h"
#include "../kernel.h"
#include "../mm/heap.h"
#include "../drivers/rtl8139.h"
#include "../drivers/timer.h"

#define TCP_HDR_LEN     20
#define TCP_CONNECT_MS  3000
#define TCP_RECV_MS     5000
#define TCP_CLOSE_MS    2000

/* Active connection pointer — only one at a time */
static tcp_conn_t *active_conn = 0;
static uint16_t    tcp_next_port = 49152;

/* ── checksum ─────────────────────────────────────────────────────────────── */
static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip,
                              const uint8_t *seg, uint16_t seg_len) {
    uint32_t sum = 0;
    /* pseudo-header: src_ip, dst_ip, 0x00, proto=6, tcp_length */
    sum += (src_ip >> 16) & 0xFFFF;
    sum += (src_ip)       & 0xFFFF;
    sum += (dst_ip >> 16) & 0xFFFF;
    sum += (dst_ip)       & 0xFFFF;
    sum += 0x0006;
    sum += seg_len;
    /* TCP segment */
    for (int i = 0; i + 1 < seg_len; i += 2)
        sum += (uint32_t)((seg[i] << 8) | seg[i+1]);
    if (seg_len & 1) sum += (uint32_t)(seg[seg_len-1] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

/* ── send raw segment ─────────────────────────────────────────────────────── */
static int tcp_send_seg(tcp_conn_t *c, const uint8_t *data,
                        uint16_t data_len, uint8_t flags) {
    uint16_t seg_len = (uint16_t)(TCP_HDR_LEN + data_len);
    uint8_t *seg     = (uint8_t *)kmalloc(seg_len);
    if (!seg) return -1;

    seg[0]  = (uint8_t)(c->local_port  >> 8);
    seg[1]  = (uint8_t)(c->local_port  & 0xFF);
    seg[2]  = (uint8_t)(c->remote_port >> 8);
    seg[3]  = (uint8_t)(c->remote_port & 0xFF);
    seg[4]  = (uint8_t)(c->seq >> 24);
    seg[5]  = (uint8_t)(c->seq >> 16);
    seg[6]  = (uint8_t)(c->seq >>  8);
    seg[7]  = (uint8_t)(c->seq);
    seg[8]  = (uint8_t)(c->ack >> 24);
    seg[9]  = (uint8_t)(c->ack >> 16);
    seg[10] = (uint8_t)(c->ack >>  8);
    seg[11] = (uint8_t)(c->ack);
    seg[12] = 0x50;   /* data offset = 5 (20 bytes), reserved = 0 */
    seg[13] = flags;
    seg[14] = 0xFF; seg[15] = 0xFF;  /* window = 65535 */
    seg[16] = 0;    seg[17] = 0;     /* checksum placeholder */
    seg[18] = 0;    seg[19] = 0;     /* urgent pointer */
    for (uint16_t i = 0; i < data_len; i++) seg[TCP_HDR_LEN + i] = data[i];

    uint16_t ck = tcp_checksum(eth_our_ip, c->remote_ip, seg, seg_len);
    seg[16] = (uint8_t)(ck >> 8);
    seg[17] = (uint8_t)(ck & 0xFF);

    int ret = ip_send(c->remote_ip, 6, seg, seg_len);
    kfree(seg);
    return ret;
}

/* ── tcp_receive (called from ip_receive when proto=6) ───────────────────── */
void tcp_receive(const uint8_t *data, uint16_t len, uint32_t src_ip) {
    if (len < TCP_HDR_LEN || !active_conn) return;
    tcp_conn_t *c = active_conn;

    uint16_t src_port  = (uint16_t)((data[0] << 8) | data[1]);
    uint16_t dst_port  = (uint16_t)((data[2] << 8) | data[3]);
    uint32_t seg_seq   = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16)
                       | ((uint32_t)data[6] <<  8) |  data[7];
    uint8_t  data_off  = (uint8_t)((data[12] >> 4) * 4);
    uint8_t  flags     = data[13];

    if (src_ip != c->remote_ip || src_port != c->remote_port
        || dst_port != c->local_port) return;

    if (flags & TCP_FLAG_RST) {
        c->state = TCP_STATE_CLOSED;
        return;
    }

    if (c->state == TCP_STATE_SYN_SENT) {
        if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
            c->ack = seg_seq + 1;
            c->seq++;                       /* our SYN counts as 1 byte */
            c->state = TCP_STATE_ESTABLISHED;
            tcp_send_seg(c, 0, 0, TCP_FLAG_ACK);
        }
        return;
    }

    if (c->state == TCP_STATE_ESTABLISHED) {
        if (flags & TCP_FLAG_FIN) {
            c->ack = seg_seq + 1;
            c->state = TCP_STATE_CLOSE_WAIT;
            tcp_send_seg(c, 0, 0, TCP_FLAG_ACK);
            return;
        }
        if (data_off < len) {
            uint16_t plen = (uint16_t)(len - data_off);
            if (plen > 0 && c->rx_len + plen <= TCP_RX_BUF_SIZE) {
                for (uint16_t i = 0; i < plen; i++)
                    c->rx_buf[c->rx_len + i] = data[data_off + i];
                c->rx_len  += plen;
                c->ack      = seg_seq + plen;
                tcp_send_seg(c, 0, 0, TCP_FLAG_ACK);
            }
        }
        return;
    }

    if (c->state == TCP_STATE_FIN_WAIT) {
        if (flags & TCP_FLAG_ACK) {
            if (flags & TCP_FLAG_FIN) {
                c->ack = seg_seq + 1;
                tcp_send_seg(c, 0, 0, TCP_FLAG_ACK);
            }
            c->state = TCP_STATE_CLOSED;
        }
        return;
    }
}

/* ── public API ──────────────────────────────────────────────────────────── */
int tcp_connect(tcp_conn_t *conn, uint32_t ip, uint16_t port) {
    /* zero out conn */
    uint8_t *p = (uint8_t *)conn;
    for (int i = 0; i < (int)sizeof(tcp_conn_t); i++) p[i] = 0;

    conn->remote_ip   = ip;
    conn->remote_port = port;
    conn->local_port  = tcp_next_port++;
    conn->seq         = (uint32_t)(timer_get_ticks() & 0xFFFFFFFFU) | 1U;
    conn->ack         = 0;
    conn->state       = TCP_STATE_SYN_SENT;
    active_conn       = conn;

    /* Send SYN */
    if (tcp_send_seg(conn, 0, 0, TCP_FLAG_SYN) < 0) {
        conn->state = TCP_STATE_CLOSED;
        return -1;
    }

    uint64_t deadline = timer_get_ticks() + TCP_CONNECT_MS;
    while (timer_get_ticks() < deadline) {
        rtl8139_receive();
        if (conn->state == TCP_STATE_ESTABLISHED) {
            klog(LOG_INFO, "TCP: connected to %d.%d.%d.%d:%d",
                 (int)(ip >> 24) & 0xFF, (int)(ip >> 16) & 0xFF,
                 (int)(ip >>  8) & 0xFF, (int)(ip) & 0xFF, (int)port);
            return 0;
        }
        if (conn->state == TCP_STATE_CLOSED) break;
    }

    conn->state = TCP_STATE_CLOSED;
    klog(LOG_WARN, "TCP: connect timeout");
    return -1;
}

int tcp_send(tcp_conn_t *conn, const uint8_t *data, uint16_t len) {
    if (conn->state != TCP_STATE_ESTABLISHED) return -1;
    int ret = tcp_send_seg(conn, data, len, TCP_FLAG_PSH | TCP_FLAG_ACK);
    if (ret == 0) conn->seq += len;
    return ret;
}

int tcp_recv(tcp_conn_t *conn, uint8_t *buf, uint16_t maxlen,
             uint32_t timeout_ms) {
    uint64_t deadline = timer_get_ticks() + timeout_ms;
    while (timer_get_ticks() < deadline) {
        rtl8139_receive();
        if (conn->rx_len > 0) {
            uint16_t n = conn->rx_len < maxlen ? conn->rx_len : maxlen;
            for (uint16_t i = 0; i < n; i++) buf[i] = conn->rx_buf[i];
            /* Shift any remaining unread bytes to the front of the buffer
             * instead of discarding them — fixes truncation of large HTTP
             * responses that span multiple tcp_recv() calls.             */
            uint16_t remaining = (uint16_t)(conn->rx_len - n);
            for (uint16_t i = 0; i < remaining; i++)
                conn->rx_buf[i] = conn->rx_buf[n + i];
            conn->rx_len = remaining;
            return (int)n;
        }
        /* connection closed by remote */
        if (conn->state == TCP_STATE_CLOSE_WAIT
            || conn->state == TCP_STATE_CLOSED) {
            /* Drain any final data the server sent before FIN */
            if (conn->rx_len > 0) {
                uint16_t n = conn->rx_len < maxlen ? conn->rx_len : maxlen;
                for (uint16_t i = 0; i < n; i++) buf[i] = conn->rx_buf[i];
                conn->rx_len = 0;
                return (int)n;
            }
            return 0;
        }
    }
    return 0;
}

void tcp_close(tcp_conn_t *conn) {
    if (conn->state == TCP_STATE_ESTABLISHED) {
        conn->state = TCP_STATE_FIN_WAIT;
        tcp_send_seg(conn, 0, 0, TCP_FLAG_FIN | TCP_FLAG_ACK);
        conn->seq++;

        uint64_t deadline = timer_get_ticks() + TCP_CLOSE_MS;
        while (timer_get_ticks() < deadline && conn->state != TCP_STATE_CLOSED) {
            rtl8139_receive();
        }
    }
    conn->state = TCP_STATE_CLOSED;
    if (active_conn == conn) active_conn = 0;
    klog(LOG_INFO, "TCP: connection closed");
}
