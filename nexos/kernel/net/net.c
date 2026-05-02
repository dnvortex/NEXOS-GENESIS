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
    klog(LOG_INFO, "Sending ARP request to 10.0.2.2 ...");
    if (arp_request(eth_gw_ip, gw_mac)) {
        klog(LOG_INFO,
             "ARP resolved: 10.0.2.2 -> %02x:%02x:%02x:%02x:%02x:%02x",
             gw_mac[0], gw_mac[1], gw_mac[2],
             gw_mac[3], gw_mac[4], gw_mac[5]);

        klog(LOG_INFO, "Sending ping to 10.0.2.2 ...");
        int rtt = icmp_send_echo(eth_gw_ip);
        if (rtt >= 0)
            klog(LOG_INFO, "Ping reply received from 10.0.2.2 (%d ms)", rtt);
        else
            klog(LOG_WARN, "Ping timed out (no reply from 10.0.2.2)");
    } else {
        klog(LOG_WARN, "ARP timed out: no reply from 10.0.2.2");
    }

    klog(LOG_INFO, "Network stack ready (Ethernet/ARP/IPv4/ICMP)");
}
