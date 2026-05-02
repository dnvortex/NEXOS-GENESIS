/* NexOS — kernel/drivers/wifi.h | Simulated 802.11n WiFi driver | MIT License */
#ifndef WIFI_H
#define WIFI_H

#include <stdint.h>

#define WIFI_MAX_APS     8
#define WIFI_SSID_LEN    32

typedef struct {
    char ssid[WIFI_SSID_LEN];
    int  signal;       /* signal strength 0-100 % */
    int  encrypted;    /* 1 = WPA2 */
} wifi_ap_t;

void        wifi_init(void);
int         wifi_scan(wifi_ap_t *out, int max);
int         wifi_connect(const char *ssid, const char *password);
void        wifi_disconnect(void);
int         wifi_is_connected(void);
const char *wifi_get_ssid(void);
int         wifi_get_signal(void);

#endif
