/* NexOS — kernel/drivers/rtl8139.c | RTL8139 NIC driver | MIT License
 *
 * Polling-based (no IRQ wired).  Transmit uses the 4-descriptor round-robin.
 * Receive drains the 32 KB+16 ring on demand (called from shell ping/ifconfig).
 * Physical address == virtual address because heap is in the 32 MB identity map.
 */
#include "rtl8139.h"
#include "../kernel.h"
#include "../mm/heap.h"
#include "../drivers/pci.h"

/* ── RTL8139 register offsets (from I/O base) ──────────────────────────── */
#define RTL_MAC0      0x00   /* MAC address bytes 0-5                       */
#define RTL_MAR0      0x08   /* Multicast filter 0-7                        */
#define RTL_TSD0      0x10   /* TX status descriptors  (4×4 = offsets +0,4,8,C) */
#define RTL_TSAD0     0x20   /* TX start addresses     (4×4 = offsets +0,4,8,C) */
#define RTL_RBSTART   0x30   /* RX ring buffer start (32-bit phys)          */
#define RTL_CR        0x37   /* Command register                             */
#define RTL_CAPR      0x38   /* Current Address of Packet Read (write only) */
#define RTL_CBR       0x3A   /* Current Buffer Address (read only)           */
#define RTL_IMR       0x3C   /* Interrupt Mask Register                      */
#define RTL_ISR       0x3E   /* Interrupt Status Register                    */
#define RTL_TCR       0x40   /* TX Configuration Register                    */
#define RTL_RCR       0x44   /* RX Configuration Register                    */
#define RTL_CONFIG1   0x52   /* Configuration Register 1                     */

/* CR bits */
#define CR_RST        0x10
#define CR_RE         0x08
#define CR_TE         0x04
#define CR_BUFE       0x01   /* RX buffer empty when set                     */

/* ISR / IMR bits */
#define ISR_ROK       0x0001  /* RX OK */
#define ISR_TOK       0x0004  /* TX OK */

/* TX descriptor size */
#define RTL_TX_BUF_SIZE  2048
#define RTL_TX_DESC_CNT  4

/* RX ring: 32 KB + 16 bytes (RBLEN = 10b = 2 in RCR bits 11-12) */
#define RTL_RX_BUF_SIZE  (32768 + 16)
#define RTL_RCR_VALUE    (0x0F | (1<<7) | (2<<11)) /* accept all + wrap + 32 KB */

/* ── Driver state ────────────────────────────────────────────────────────── */
static int       rtl_detected  = 0;
static uint32_t  rtl_iobase    = 0;
static uint8_t   rtl_mac[6]    = {0};
static uint8_t  *rtl_rx_buf    = NULL;
static uint8_t  *rtl_tx_buf[RTL_TX_DESC_CNT];
static int       rtl_tx_slot   = 0;
static uint16_t  rtl_rx_ptr    = 0;   /* software read pointer into ring */
static uint32_t  rtl_rx_count  = 0;
static uint32_t  rtl_tx_count  = 0;

/* Registered receive callback (set by callers that want to inspect packets) */
static void (*rtl_rx_callback)(const uint8_t *pkt, uint16_t len) = NULL;

void rtl8139_set_rx_callback(void (*cb)(const uint8_t *, uint16_t)) {
    rtl_rx_callback = cb;
}

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static void rtl_mem_zero(void *p, size_t n) {
    uint8_t *b = (uint8_t *)p;
    for (size_t i = 0; i < n; i++) b[i] = 0;
}
static void rtl_mem_copy(void *d, const void *s, size_t n) {
    uint8_t *dd = (uint8_t *)d;
    const uint8_t *ss = (const uint8_t *)s;
    for (size_t i = 0; i < n; i++) dd[i] = ss[i];
}

/* ── Initialisation ──────────────────────────────────────────────────────── */
int rtl8139_init(void) {
    /* a) Scan PCI for vendor=0x10EC device=0x8139 */
    int found_bus = -1, found_dev = -1, found_func = -1;
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].vendor_id == 0x10EC &&
            pci_devices[i].device_id == 0x8139) {
            found_bus  = pci_devices[i].bus;
            found_dev  = pci_devices[i].device;
            found_func = pci_devices[i].function;
            break;
        }
    }
    if (found_bus < 0) {
        klog(LOG_INFO, "RTL8139: not found");
        return 0;
    }

    /* b/c) Read BAR0 — I/O space (bit 0 = 1) */
    uint32_t bar0 = pci_read((uint8_t)found_bus, (uint8_t)found_dev,
                              (uint8_t)found_func, 0x10);
    if (!(bar0 & 1)) {
        klog(LOG_WARN, "RTL8139: BAR0 is not I/O space (val=0x%x)", bar0);
        return 0;
    }
    rtl_iobase = bar0 & ~(uint32_t)3;

    /* d) Enable PCI bus mastering (Command register bit 2) */
    uint32_t cmd = pci_read((uint8_t)found_bus, (uint8_t)found_dev,
                             (uint8_t)found_func, 0x04);
    pci_write((uint8_t)found_bus, (uint8_t)found_dev,
              (uint8_t)found_func, 0x04, cmd | 0x04);

    /* e) Power on */
    io_outb(rtl_iobase + RTL_CONFIG1, 0x00);

    /* f) Software reset — write 0x10 to CR, poll until bit 4 clears */
    io_outb(rtl_iobase + RTL_CR, CR_RST);
    for (int i = 0; i < 1000000; i++) {
        if (!(io_inb(rtl_iobase + RTL_CR) & CR_RST)) break;
    }

    /* g) Read MAC address */
    for (int i = 0; i < 6; i++)
        rtl_mac[i] = io_inb(rtl_iobase + RTL_MAC0 + i);

    klog(LOG_INFO,
         "RTL8139: found at PCI %02x:%02x.%x iobase=0x%x "
         "MAC=%02x:%02x:%02x:%02x:%02x:%02x",
         found_bus, found_dev, found_func, rtl_iobase,
         rtl_mac[0], rtl_mac[1], rtl_mac[2],
         rtl_mac[3], rtl_mac[4], rtl_mac[5]);

    /* ── 3B: RX ring buffer ─────────────────────────────────────────────── */
    rtl_rx_buf = (uint8_t *)kmalloc(RTL_RX_BUF_SIZE);
    if (!rtl_rx_buf) { klog(LOG_ERROR, "RTL8139: cannot allocate RX buffer"); return -1; }
    rtl_mem_zero(rtl_rx_buf, RTL_RX_BUF_SIZE);
    rtl_rx_ptr = 0;

    /* Write physical address of RX buffer (identity mapped → phys == virt) */
    io_outl(rtl_iobase + RTL_RBSTART, (uint32_t)(uintptr_t)rtl_rx_buf);

    /* IMR: enable ROK + TOK */
    io_outw(rtl_iobase + RTL_IMR, ISR_ROK | ISR_TOK);

    /* RCR: accept all packets, wrap mode, 32 KB ring */
    io_outl(rtl_iobase + RTL_RCR, RTL_RCR_VALUE);

    /* ── TX buffers ────────────────────────────────────────────────────── */
    for (int i = 0; i < RTL_TX_DESC_CNT; i++) {
        rtl_tx_buf[i] = (uint8_t *)kmalloc(RTL_TX_BUF_SIZE);
        if (!rtl_tx_buf[i]) { klog(LOG_ERROR, "RTL8139: cannot allocate TX buffer %d", i); return -1; }
        rtl_mem_zero(rtl_tx_buf[i], RTL_TX_BUF_SIZE);
        /* Write physical TX address upfront */
        io_outl(rtl_iobase + RTL_TSAD0 + (uint32_t)(i * 4),
                (uint32_t)(uintptr_t)rtl_tx_buf[i]);
    }

    /* Enable TX + RX */
    io_outb(rtl_iobase + RTL_CR, CR_TE | CR_RE);

    rtl_detected = 1;
    klog(LOG_INFO, "RTL8139: RX/TX enabled, RX buf at 0x%x (%u KB)",
         (uint32_t)(uintptr_t)rtl_rx_buf, RTL_RX_BUF_SIZE / 1024);
    klog(LOG_INFO, "RTL8139 initialized");
    return 1;
}

/* ── Public API ──────────────────────────────────────────────────────────── */
int  rtl8139_found(void)           { return rtl_detected; }
void rtl8139_get_mac(uint8_t m[6]) { for (int i=0;i<6;i++) m[i]=rtl_mac[i]; }
uint32_t rtl8139_get_rx_count(void){ return rtl_rx_count; }
uint32_t rtl8139_get_tx_count(void){ return rtl_tx_count; }

/* ── Transmit ────────────────────────────────────────────────────────────── */
int rtl8139_send(const uint8_t *data, uint16_t len) {
    if (!rtl_detected) return -1;
    if (len > RTL_TX_BUF_SIZE) len = (uint16_t)RTL_TX_BUF_SIZE;

    int slot = rtl_tx_slot % RTL_TX_DESC_CNT;
    rtl_tx_slot++;

    /* Copy payload into TX buffer for this slot */
    rtl_mem_copy(rtl_tx_buf[slot], data, len);

    /* TSAD already set during init; write len to TSD to start TX */
    /* TSD bit layout: bits 0-12 = size, bits 16-20 = early TX threshold */
    io_outl(rtl_iobase + RTL_TSD0 + (uint32_t)(slot * 4), (uint32_t)len);

    /* Poll TOK (bit 15) — timeout ~100 ms */
    for (int i = 0; i < 1000000; i++) {
        uint32_t tsd = io_inl(rtl_iobase + RTL_TSD0 + (uint32_t)(slot * 4));
        if (tsd & (1u << 15)) { /* TOK */
            rtl_tx_count++;
            return 0;
        }
        if (tsd & (1u << 30)) { /* TABT */
            klog(LOG_WARN, "RTL8139: TX abort on slot %d", slot);
            return -1;
        }
    }
    klog(LOG_WARN, "RTL8139: TX timeout on slot %d", slot);
    return -1;
}

/* ── Receive (poll & drain the ring) ─────────────────────────────────────── */
void rtl8139_receive(void) {
    if (!rtl_detected) return;

    while (!(io_inb(rtl_iobase + RTL_CR) & CR_BUFE)) {
        /* RX packet header is 4 bytes: [16-bit status][16-bit length] */
        uint32_t offset = rtl_rx_ptr & 0x7FFF; /* 32 KB mask */
        uint32_t hdr    = *(uint32_t *)(rtl_rx_buf + offset);
        uint16_t status = (uint16_t)(hdr & 0xFFFF);
        uint16_t rxlen  = (uint16_t)(hdr >> 16);   /* includes 4-byte CRC */

        if (!(status & 0x0001) || rxlen < 4) {
            /* Not ROK — reset ring pointer to CBR */
            rtl_rx_ptr = io_inw(rtl_iobase + RTL_CBR);
            io_outw(rtl_iobase + RTL_CAPR, rtl_rx_ptr - 0x10);
            break;
        }

        uint16_t pkt_len = rxlen - 4; /* strip CRC */
        uint8_t *pkt     = rtl_rx_buf + offset + 4;

        if (rtl_rx_callback && pkt_len > 0)
            rtl_rx_callback(pkt, pkt_len);

        rtl_rx_count++;

        /* Advance read pointer (4-byte aligned, RTL quirk: write CAPR = pos-0x10) */
        uint16_t new_ptr = (uint16_t)((rtl_rx_ptr + 4 + rxlen + 3) & ~3u);
        rtl_rx_ptr = new_ptr;
        io_outw(rtl_iobase + RTL_CAPR, new_ptr - 0x10);

        /* Acknowledge interrupt */
        io_outw(rtl_iobase + RTL_ISR, ISR_ROK);
    }
}

/* ── ARP reply builder ───────────────────────────────────────────────────── */
/*
 * Called when an ARP request for our IP is received.
 * Constructs and sends an ARP reply.
 */
void rtl8139_arp_reply(const uint8_t *req_pkt) {
    if (!rtl_detected) return;

    /* req_pkt points to the Ethernet header of the ARP request */
    static const uint8_t our_ip[] = RTL_OUR_IP;

    /* Check EtherType == 0x0806 (ARP) */
    if (req_pkt[12] != 0x08 || req_pkt[13] != 0x06) return;

    /* ARP payload starts at offset 14 */
    const uint8_t *arp = req_pkt + 14;
    if (arp[6] != 0x00 || arp[7] != 0x01) return; /* op != request */

    /* Check target IP */
    if (arp[24] != our_ip[0] || arp[25] != our_ip[1] ||
        arp[26] != our_ip[2] || arp[27] != our_ip[3]) return;

    uint8_t reply[42]; /* 14 Ethernet + 28 ARP */
    /* Ethernet header */
    for (int i=0;i<6;i++) reply[i]    = arp[8+i];  /* dst = sender MAC */
    for (int i=0;i<6;i++) reply[6+i]  = rtl_mac[i]; /* src = our MAC */
    reply[12] = 0x08; reply[13] = 0x06;              /* EtherType ARP */

    /* ARP payload */
    reply[14] = 0x00; reply[15] = 0x01; /* HW type Ethernet */
    reply[16] = 0x08; reply[17] = 0x00; /* Proto IPv4 */
    reply[18] = 6;                       /* HW addr len */
    reply[19] = 4;                       /* Proto addr len */
    reply[20] = 0x00; reply[21] = 0x02; /* op = reply */
    for (int i=0;i<6;i++) reply[22+i]  = rtl_mac[i]; /* sender MAC = us */
    for (int i=0;i<4;i++) reply[28+i]  = our_ip[i];  /* sender IP = us */
    for (int i=0;i<6;i++) reply[32+i]  = arp[8+i];   /* target MAC = requester */
    for (int i=0;i<4;i++) reply[38+i]  = arp[14+i];  /* target IP = requester */

    rtl8139_send(reply, 42);
    klog(LOG_INFO, "RTL8139: ARP reply sent to %d.%d.%d.%d",
         arp[14], arp[15], arp[16], arp[17]);
}
