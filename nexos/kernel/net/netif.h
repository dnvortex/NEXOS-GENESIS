/* NexOS - kernel/net/netif.h | Network interface layer | MIT License */
#ifndef NETIF_H
#define NETIF_H

#include <stdint.h>

#define NETIF_FLAG_UP   0x01
#define NETIF_MAX       4
#define NETIF_NAME_LEN  8

typedef struct {
    char    name[NETIF_NAME_LEN];
    uint8_t mac[6];
    int     flags;
    int     valid;
} netif_t;

/* Register a new interface; returns 0 on success */
int      netif_register(const char *name, const uint8_t mac[6], int flags);

/* Mark a registered interface UP by name */
void     netif_set_up(const char *name);

/* Mark a registered interface DOWN by name */
void     netif_set_down(const char *name);

/* Returns 1 if at least one interface is UP, 0 otherwise */
int      netif_is_up(void);

/* Returns pointer to first UP interface, or NULL */
netif_t *netif_get_default(void);

/* Returns pointer to interface by name, or NULL */
netif_t *netif_find(const char *name);

#endif
