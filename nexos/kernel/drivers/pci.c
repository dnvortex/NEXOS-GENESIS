/* NexOS — kernel/drivers/pci.c | Multi-bus PCI enumeration | MIT License */
#include "pci.h"
#include "../kernel.h"
#include "../drivers/serial.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

pci_device_t pci_devices[PCI_MAX_DEVICES];
int pci_device_count = 0;

uint32_t pci_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) |
                    ((uint32_t)dev << 11) | ((uint32_t)func << 8) |
                    (offset & 0xFC);
    io_outl(PCI_CONFIG_ADDR, addr);
    return io_inl(PCI_CONFIG_DATA);
}

void pci_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) |
                    ((uint32_t)dev << 11) | ((uint32_t)func << 8) |
                    (offset & 0xFC);
    io_outl(PCI_CONFIG_ADDR, addr);
    io_outl(PCI_CONFIG_DATA, val);
}

/* ── Recursive bus scanner ───────────────────────────────────────────────── */
static uint8_t scanned_buses[256];   /* visited bitmap to avoid loops */

static void pci_scan_bus(uint8_t bus) {
    if (scanned_buses[bus]) return;
    scanned_buses[bus] = 1;

    for (uint8_t dev = 0; dev < 32; dev++) {
        for (uint8_t func = 0; func < 8; func++) {
            uint32_t id     = pci_read(bus, dev, func, 0x00);
            uint16_t vendor = id & 0xFFFF;
            if (vendor == 0xFFFF) {
                if (func == 0) break;   /* no device at function 0 → skip slot */
                continue;
            }

            uint16_t device_id  = (id >> 16) & 0xFFFF;
            uint32_t class_reg  = pci_read(bus, dev, func, 0x08);
            uint8_t  class_code = (class_reg >> 24) & 0xFF;
            uint8_t  subclass   = (class_reg >> 16) & 0xFF;
            uint8_t  prog_if    = (class_reg >>  8) & 0xFF;
            uint32_t hdr_reg    = pci_read(bus, dev, func, 0x0C);
            uint8_t  hdr_type   = (hdr_reg >> 16) & 0xFF;

            if (pci_device_count < PCI_MAX_DEVICES) {
                pci_device_t *d     = &pci_devices[pci_device_count++];
                d->bus        = bus;
                d->device     = dev;
                d->function   = func;
                d->vendor_id  = vendor;
                d->device_id  = device_id;
                d->class_code = class_code;
                d->subclass   = subclass;
                d->prog_if    = prog_if;
                d->header_type= hdr_type;

                serial_printf("[PCI] %02x:%02x.%x vendor=%04x dev=%04x class=%02x:%02x\n",
                    bus, dev, func, vendor, device_id, class_code, subclass);
            }

            /* PCI-to-PCI bridge: header_type&0x7F==1, class=0x06, sub=0x04
             * Read register 0x18 to get secondary bus number, then recurse. */
            if ((hdr_type & 0x7F) == 1 && class_code == 0x06 && subclass == 0x04) {
                uint32_t bus_reg = pci_read(bus, dev, func, 0x18);
                uint8_t  sec     = (bus_reg >> 8) & 0xFF;
                if (sec > 0 && sec != bus) {
                    serial_printf("[PCI] Bridge %02x:%02x.%x -> secondary bus %02x\n",
                                  bus, dev, func, sec);
                    pci_scan_bus(sec);
                }
            }

            /* Single-function device: skip further functions */
            if (func == 0 && !(hdr_type & 0x80)) break;
        }
    }
}

void pci_init(void) {
    pci_device_count = 0;
    for (int i = 0; i < 256; i++) scanned_buses[i] = 0;

    /* Always scan bus 0 first (root complex) */
    pci_scan_bus(0);

    /* QEMU q35: RTL8139 is a legacy PCI device placed behind an implicit
     * PCIe-to-PCI bridge.  If it was not found via bridge recursion (some
     * QEMU versions omit bridge class=0x06/0x04), scan bus 1 explicitly.  */
    int nic_found = 0;
    for (int i = 0; i < pci_device_count; i++) {
        uint16_t v = pci_devices[i].vendor_id, d = pci_devices[i].device_id;
        if ((v == 0x10EC && d == 0x8139) ||     /* RTL8139     */
            (v == 0x8086 && d == 0x100E) ||     /* e1000       */
            (v == 0x1AF4 && d == 0x1000)) {     /* virtio-net  */
            nic_found = 1; break;
        }
    }
    if (!nic_found) {
        serial_printf("[PCI] No NIC on bus 0; scanning bus 1 (q35 fallback)\n");
        pci_scan_bus(1);
        /* Try bus 2 as well — seen on some QEMU versions */
        pci_scan_bus(2);
    }

    klog(LOG_INFO, "PCI: found %d device(s) across all buses", pci_device_count);
}
