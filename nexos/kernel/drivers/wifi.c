/* NexOS — kernel/drivers/wifi.c | Simulated 802.11n WiFi driver | MIT License
 *
 * Simulates a PCIe 802.11n adapter (backed by QEMU's user-mode network).
 * Registers a wlan0 netif when connected, giving the shell wifi/ifconfig cmds.
 */
#include "wifi.h"
#include "../net/netif.h"
#include "../kernel.h"

/* ── Hard-coded AP scan list ─────────────────────────────────────────────── */
static wifi_ap_t ap_list[] = {
    { "NexOS_Home",      95, 1 },
    { "HomeNetwork",     82, 1 },
    { "NETGEAR-5G",      74, 1 },
    { "Neighbors_WiFi",  59, 1 },
    { "Free_Public_WiFi",42, 0 },
    { "DIRECT-TV-4B2F",  31, 0 },
};
#define AP_COUNT  ((int)(sizeof(ap_list)/sizeof(ap_list[0])))

/* ── State ───────────────────────────────────────────────────────────────── */
static int  wifi_up      = 0;
static char wifi_ssid[WIFI_SSID_LEN] = {0};
static int  wifi_sig     = 0;

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static void w_strcpy(char *d, const char *s, int max) {
    int i = 0;
    while (i < max - 1 && s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
}
static int w_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

/* ── Public API ──────────────────────────────────────────────────────────── */
void wifi_init(void) {
    wifi_up  = 0;
    wifi_ssid[0] = 0;
    wifi_sig = 0;
    klog(LOG_INFO, "WiFi: simulated 802.11n adapter ready (wlan0, %d APs visible)",
         AP_COUNT);
}

int wifi_scan(wifi_ap_t *out, int max) {
    int n = AP_COUNT < max ? AP_COUNT : max;
    for (int i = 0; i < n; i++) out[i] = ap_list[i];
    return n;
}

int wifi_connect(const char *ssid, const char *password) {
    (void)password;   /* WPA2 authentication stub */

    /* Find matching AP */
    int found = -1;
    for (int i = 0; i < AP_COUNT; i++) {
        if (w_strcmp(ap_list[i].ssid, ssid) == 0) { found = i; break; }
    }
    if (found < 0) {
        klog(LOG_WARN, "WiFi: SSID '%s' not found", ssid);
        return -1;
    }

    /* Disconnect existing */
    if (wifi_up) wifi_disconnect();

    /* Register wlan0 with a synthetic MAC derived from SSID */
    uint8_t mac[6] = { 0x52, 0x54, 0x00, 0x9A, 0x01, 0x00 };
    mac[5] = (uint8_t)(found * 3 + 1);
    netif_register("wlan0", mac, NETIF_FLAG_UP);

    wifi_up  = 1;
    wifi_sig = ap_list[found].signal;
    w_strcpy(wifi_ssid, ssid, WIFI_SSID_LEN);

    klog(LOG_INFO, "WiFi: connected to '%s' (signal %d%%)", ssid, wifi_sig);
    return 0;
}

void wifi_disconnect(void) {
    if (!wifi_up) return;
    wifi_up      = 0;
    wifi_ssid[0] = 0;
    wifi_sig     = 0;
    /* Mark wlan0 DOWN so netif_is_up() returns the correct state */
    netif_set_down("wlan0");
    klog(LOG_INFO, "WiFi: disconnected");
}

int         wifi_is_connected(void) { return wifi_up; }
const char *wifi_get_ssid(void)     { return wifi_ssid; }
int         wifi_get_signal(void)   { return wifi_sig; }
