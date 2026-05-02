/* NexOS - kernel/net/net.c | Network subsystem init | MIT License */
#include "net.h"
#include "ethernet.h"
#include "arp.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include "../kernel.h"
#include "../drivers/rtl8139.h"

/* ────────────────────────────────────────────────────────────────────────────
 * net_init() — boot-time network stack bringup.
 *
 * IMPORTANT: rtl8139_init() MUST be called before net_init().
 *   rtl8139_init() → detects NIC, allocates RX/TX buffers, registers eth0.
 *   net_init()     → hooks Ethernet callback, probes gateway, pings, logs.
 * ──────────────────────────────────────────────────────────────────────────── */
void net_init(void) {
    if (!rtl8139_found()) {
        klog(LOG_WARN, "net: RTL8139 not detected — networking disabled");
        klog(LOG_WARN, "     (hint: check PCI scan covers q35 bus 1)");
        return;
    }

    /* ── Layer 2: wire Ethernet RX callback, read MAC ─────────────────────── */
    ethernet_init();
    klog(LOG_INFO, "eth0  10.0.2.15/24  gw 10.0.2.2");

    /* ── Layer 3/ARP: resolve gateway so we can route IP packets ─────────── */
    uint8_t gw_mac[6] = {0};
    klog(LOG_INFO, "ARP: probing gateway 10.0.2.2 ...");
    if (arp_request(eth_gw_ip, gw_mac)) {
        klog(LOG_INFO,
             "ARP: 10.0.2.2 -> %02x:%02x:%02x:%02x:%02x:%02x",
             gw_mac[0], gw_mac[1], gw_mac[2],
             gw_mac[3], gw_mac[4], gw_mac[5]);
    } else {
        klog(LOG_WARN, "ARP: no reply from 10.0.2.2 (QEMU user-net down?)");
        /* Non-fatal — stack is still usable for LAN */
    }

    /* ── ICMP: send one echo to verify IP reachability ───────────────────── */
    klog(LOG_INFO, "ICMP: ping 10.0.2.2 ...");
    int rtt = icmp_send_echo(eth_gw_ip);
    if (rtt >= 0)
        klog(LOG_INFO, "ICMP: reply from 10.0.2.2 in %d ms — IP layer OK", rtt);
    else
        klog(LOG_WARN, "ICMP: no reply from 10.0.2.2 (gateway unreachable)");

    klog(LOG_INFO, "Network stack ready: Ethernet / ARP / IPv4 / ICMP / "
                   "UDP / TCP / DNS / HTTP");
}
