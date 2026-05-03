/* NexOS - kernel/net/netif.c | Network interface layer | MIT License */
#include "netif.h"
#include "../kernel.h"

static netif_t netif_table[NETIF_MAX];

static int nif_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static void nif_strcpy(char *d, const char *s, int max) {
    int i = 0;
    while (i < max - 1 && s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
}

int netif_register(const char *name, const uint8_t mac[6], int flags) {
    /* Update existing entry if the same name is already registered
     * (e.g. wifi reconnect must not create a duplicate wlan0 entry) */
    for (int i = 0; i < NETIF_MAX; i++) {
        if (netif_table[i].valid && nif_strcmp(netif_table[i].name, name) == 0) {
            for (int j = 0; j < 6; j++) netif_table[i].mac[j] = mac[j];
            netif_table[i].flags = flags;
            klog(LOG_INFO, "Network interface %s updated", name);
            return 0;
        }
    }
    /* Insert into first empty slot */
    for (int i = 0; i < NETIF_MAX; i++) {
        if (!netif_table[i].valid) {
            nif_strcpy(netif_table[i].name, name, NETIF_NAME_LEN);
            for (int j = 0; j < 6; j++) netif_table[i].mac[j] = mac[j];
            netif_table[i].flags = flags;
            netif_table[i].valid = 1;
            klog(LOG_INFO, "Network interface %s up", name);
            return 0;
        }
    }
    klog(LOG_WARN, "netif: interface table full");
    return -1;
}

void netif_set_up(const char *name) {
    for (int i = 0; i < NETIF_MAX; i++) {
        if (netif_table[i].valid && nif_strcmp(netif_table[i].name, name) == 0) {
            netif_table[i].flags |= NETIF_FLAG_UP;
            return;
        }
    }
}

void netif_set_down(const char *name) {
    for (int i = 0; i < NETIF_MAX; i++) {
        if (netif_table[i].valid && nif_strcmp(netif_table[i].name, name) == 0) {
            netif_table[i].flags &= ~NETIF_FLAG_UP;
            klog(LOG_INFO, "Network interface %s down", name);
            return;
        }
    }
}

int netif_is_up(void) {
    for (int i = 0; i < NETIF_MAX; i++) {
        if (netif_table[i].valid && (netif_table[i].flags & NETIF_FLAG_UP))
            return 1;
    }
    return 0;
}

netif_t *netif_get_default(void) {
    for (int i = 0; i < NETIF_MAX; i++) {
        if (netif_table[i].valid && (netif_table[i].flags & NETIF_FLAG_UP))
            return &netif_table[i];
    }
    return 0;
}

netif_t *netif_find(const char *name) {
    for (int i = 0; i < NETIF_MAX; i++) {
        if (netif_table[i].valid && nif_strcmp(netif_table[i].name, name) == 0)
            return &netif_table[i];
    }
    return 0;
}
