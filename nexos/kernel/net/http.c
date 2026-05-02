/* NexOS - kernel/net/http.c | HTTP/1.0 GET client | MIT License */
#include "http.h"
#include "tcp.h"
#include "dns.h"
#include "ethernet.h"
#include "../kernel.h"
#include "../mm/heap.h"
#include "../drivers/timer.h"

#define HTTP_BUF_SIZE   (64 * 1024)   /* 64 KB response buffer */
#define HTTP_RECV_MS    8000

/* ── static TCP connection (one HTTP request at a time) ────────────────────  */
static tcp_conn_t http_conn;

/* ── string helpers ──────────────────────────────────────────────────────── */
static void h_strcpy(char *d, const char *s, int max) {
    int i = 0;
    while (i < max - 1 && s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
}
static int h_strncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}
static int h_atoi(const char *s) {
    int v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return v;
}

/* ── parse URL: http://hostname[:port]/path ──────────────────────────────── */
static int parse_url(const char *url, char *host_out, int host_max,
                     uint16_t *port_out, char *path_out, int path_max) {
    const char *p = url;
    /* skip "http://" */
    if (h_strncmp(p, "http://", 7) == 0) p += 7;

    /* host[:port] ends at first '/' or end of string */
    int hi = 0;
    while (*p && *p != '/' && *p != ':' && hi < host_max - 1)
        host_out[hi++] = *p++;
    host_out[hi] = 0;

    *port_out = 80;
    if (*p == ':') {
        p++;
        *port_out = (uint16_t)h_atoi(p);
        while (*p >= '0' && *p <= '9') p++;
    }

    /* path */
    if (*p == '/') {
        h_strcpy(path_out, p, path_max);
    } else {
        path_out[0] = '/'; path_out[1] = 0;
    }
    return 0;
}

/* ── build GET request into buf ─────────────────────────────────────────── */
static int build_request(uint8_t *buf, int buf_sz,
                         const char *host, const char *path) {
    /* "GET /path HTTP/1.0\r\nHost: hostname\r\nUser-Agent: NexOS/1.0\r\nConnection: close\r\n\r\n" */
    const char *method = "GET ";
    const char *ver    = " HTTP/1.0\r\nHost: ";
    const char *ua     = "\r\nUser-Agent: NexOS/1.0\r\nConnection: close\r\n\r\n";
    int pos = 0;
    for (int i = 0; method[i] && pos < buf_sz - 1; i++) buf[pos++] = (uint8_t)method[i];
    for (int i = 0; path[i]   && pos < buf_sz - 1; i++) buf[pos++] = (uint8_t)path[i];
    for (int i = 0; ver[i]    && pos < buf_sz - 1; i++) buf[pos++] = (uint8_t)ver[i];
    for (int i = 0; host[i]   && pos < buf_sz - 1; i++) buf[pos++] = (uint8_t)host[i];
    for (int i = 0; ua[i]     && pos < buf_sz - 1; i++) buf[pos++] = (uint8_t)ua[i];
    return pos;
}

/* ── parse status line: "HTTP/1.x NNN ..." → NNN ────────────────────────── */
static int parse_status(const uint8_t *data, uint32_t len) {
    /* Find first space */
    uint32_t i = 0;
    while (i < len && data[i] != ' ') i++;
    if (i >= len) return 0;
    i++;
    int code = 0;
    while (i < len && data[i] >= '0' && data[i] <= '9') {
        code = code * 10 + (data[i] - '0');
        i++;
    }
    return code;
}

/* ── find \r\n\r\n in buffer, return offset of body start ────────────────── */
static int find_body(const uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i + 3 < len; i++) {
        if (data[i]=='\r' && data[i+1]=='\n' &&
            data[i+2]=='\r' && data[i+3]=='\n')
            return (int)(i + 4);
    }
    return -1;
}

/* ── public API ──────────────────────────────────────────────────────────── */
http_response_t *http_get(const char *url) {
    char     host[128];
    char     path[256];
    uint16_t port;

    if (parse_url(url, host, sizeof(host), &port, path, sizeof(path)) < 0)
        return 0;

    /* Resolve hostname */
    uint8_t host_ip[4];
    uint32_t dst_ip;
    /* Check if it's a dotted-decimal IP first */
    int a0 = 0, a1 = 0, a2 = 0, a3 = 0, dot_count = 0;
    const char *hp = host;
    int digits = 0;
    while (*hp) {
        if (*hp >= '0' && *hp <= '9') digits++;
        else if (*hp == '.') dot_count++;
        hp++;
    }
    if (dot_count == 3 && digits > 0) {
        /* Parse as IP */
        const char *q = host;
        a0 = h_atoi(q); while (*q && *q != '.') q++; if (*q) q++;
        a1 = h_atoi(q); while (*q && *q != '.') q++; if (*q) q++;
        a2 = h_atoi(q); while (*q && *q != '.') q++; if (*q) q++;
        a3 = h_atoi(q);
        dst_ip = ((uint32_t)a0 << 24) | ((uint32_t)a1 << 16)
               | ((uint32_t)a2 <<  8) | (uint32_t)a3;
    } else {
        if (dns_resolve(host, host_ip) < 0) {
            klog(LOG_WARN, "HTTP: DNS failed for %s", host);
            return 0;
        }
        dst_ip = ((uint32_t)host_ip[0] << 24) | ((uint32_t)host_ip[1] << 16)
               | ((uint32_t)host_ip[2] <<  8) |  host_ip[3];
    }

    /* Connect */
    if (tcp_connect(&http_conn, dst_ip, port) < 0) {
        klog(LOG_WARN, "HTTP: TCP connect failed to %s:%d", host, (int)port);
        return 0;
    }

    /* Build and send GET request */
    uint8_t req[512];
    int req_len = build_request(req, (int)sizeof(req), host, path);
    if (tcp_send(&http_conn, req, (uint16_t)req_len) < 0) {
        tcp_close(&http_conn);
        return 0;
    }

    /* Receive full response */
    uint8_t *resp_buf = (uint8_t *)kmalloc(HTTP_BUF_SIZE);
    if (!resp_buf) { tcp_close(&http_conn); return 0; }

    uint32_t total = 0;
    uint64_t deadline = timer_get_ticks() + HTTP_RECV_MS;
    while (timer_get_ticks() < deadline && total < HTTP_BUF_SIZE - 1) {
        int n = tcp_recv(&http_conn, resp_buf + total,
                         (uint16_t)(HTTP_BUF_SIZE - 1 - total), 500);
        if (n > 0) {
            total += (uint32_t)n;
            deadline = timer_get_ticks() + HTTP_RECV_MS; /* reset on data */
        }
        if (http_conn.state == TCP_STATE_CLOSE_WAIT
            || http_conn.state == TCP_STATE_CLOSED) break;
    }
    tcp_close(&http_conn);

    if (total == 0) {
        kfree(resp_buf);
        klog(LOG_WARN, "HTTP: no data received");
        return 0;
    }

    /* Parse response */
    http_response_t *r = (http_response_t *)kmalloc(sizeof(http_response_t));
    if (!r) { kfree(resp_buf); return 0; }

    r->status_code = parse_status(resp_buf, total);
    int body_off   = find_body(resp_buf, total);

    if (body_off < 0 || (uint32_t)body_off >= total) {
        r->body     = 0;
        r->body_len = 0;
    } else {
        r->body_len = total - (uint32_t)body_off;
        r->body     = (uint8_t *)kmalloc(r->body_len + 1);
        if (r->body) {
            for (uint32_t i = 0; i < r->body_len; i++)
                r->body[i] = resp_buf[body_off + i];
            r->body[r->body_len] = 0;
        }
    }
    kfree(resp_buf);
    klog(LOG_INFO, "HTTP: %s status=%d body=%u bytes",
         url, r->status_code, r->body_len);
    return r;
}

void http_free(http_response_t *r) {
    if (!r) return;
    if (r->body) kfree(r->body);
    kfree(r);
}
