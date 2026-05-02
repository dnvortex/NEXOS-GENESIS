/* NexOS - kernel/net/http.h | Minimal HTTP/1.0 GET client | MIT License */
#ifndef HTTP_H
#define HTTP_H

#include <stdint.h>

typedef struct {
    int      status_code;
    uint32_t body_len;
    uint8_t *body;    /* kmalloc'd; caller must call http_free */
} http_response_t;

/* Fetch a URL via HTTP GET.
 * url must start with "http://".
 * Returns allocated response on success, NULL on failure. */
http_response_t *http_get(const char *url);

/* Free a response returned by http_get. */
void             http_free(http_response_t *r);

#endif
