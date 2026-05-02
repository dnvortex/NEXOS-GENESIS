/* NexOS - kernel/net/dns.h | DNS resolver (A records) | MIT License */
#ifndef DNS_H
#define DNS_H

#include <stdint.h>

/* Resolve hostname to IPv4 address.
 * ip_out must be uint8_t[4].
 * Returns 0 on success, -1 on timeout/error. */
int dns_resolve(const char *hostname, uint8_t ip_out[4]);

#endif
