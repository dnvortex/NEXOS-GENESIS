/* NexOS - kernel/net/net.c | Network subsystem init | MIT License */
#include "net.h"
#include "ethernet.h"
#include "arp.h"
#include "icmp.h"
#include "../kernel.h"
#include "../drivers/rtl8139.h"

void net_init(void) {
    if (!rtl8139_found()) {
        klog(LOG_WARN, "net: no NIC detected, networking disabled");
        return;
    }

    ethernet_init();

    /* ARP probe for the QEMU gateway (10.0.2.2) */
    uint8_t gw_mac[6] = {0};
    klog(LOG_INFO, "net: ARP probe for gateway 10.0.2.2 ...");
    if (arp_request(eth_gw_ip, gw_mac)) {
        klog(LOG_INFO,
             "net: gateway MAC=%02x:%02x:%02x:%02x:%02x:%02x",
             gw_mac[0], gw_mac[1], gw_mac[2],
             gw_mac[3], gw_mac[4], gw_mac[5]);

        int rtt = icmp_send_echo(eth_gw_ip);
        if (rtt >= 0)
            klog(LOG_INFO, "net: ping 10.0.2.2 reply in %d ms", rtt);
        else
            klog(LOG_WARN, "net: ping 10.0.2.2 timed out");
    } else {
        klog(LOG_WARN, "net: ARP for gateway timed out");
    }

    klog(LOG_INFO, "net: network stack ready (Ethernet/ARP/IPv4/ICMP)");
}
