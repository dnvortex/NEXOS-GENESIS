/* NexOS — kernel/gui/settings_app.h | System Settings | MIT License */
#pragma once
#include "wm.h"
#include "../drivers/wifi.h"
#include "../net/netif.h"
#include <stdint.h>

typedef struct {
    window_t  *win;
    int        tab;                     /* 0=WiFi 1=Display 2=System 3=About */
    wifi_ap_t  aps[WIFI_MAX_APS];
    int        ap_count;
    int        selected_ap;             /* -1 = none */
    char       password[64];
    int        pwd_len;
    int        pwd_focus;
    int        hover_tab;               /* -1 = none */
    int        hover_ap;                /* -1 = none */
    int        hover_connect;
    int        hover_disconnect;
    int        hover_rescan;
    int        msg_timer;               /* frames remaining to show msg */
    char       msg_buf[64];
    int        msg_ok;                  /* 1=green 0=red */
} settings_app_t;

settings_app_t *settings_create(int x, int y);
