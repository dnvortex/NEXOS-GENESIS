/* NexOS — kernel/drivers/pci.c | PCI bus 0 enumeration | MIT License */
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

void pci_init(void) {
    pci_device_count = 0;
    for (uint8_t dev = 0; dev < 32; dev++) {
        for (uint8_t func = 0; func < 8; func++) {
            uint32_t id = pci_read(0, dev, func, 0x00);
            uint16_t vendor = id & 0xFFFF;
            if (vendor == 0xFFFF) continue;

            uint16_t device_id = (id >> 16) & 0xFFFF;
            uint32_t class_reg = pci_read(0, dev, func, 0x08);
            uint8_t  class_code = (class_reg >> 24) & 0xFF;
            uint8_t  subclass   = (class_reg >> 16) & 0xFF;
            uint8_t  prog_if    = (class_reg >> 8) & 0xFF;
            uint32_t hdr_reg    = pci_read(0, dev, func, 0x0C);
            uint8_t  hdr_type   = (hdr_reg >> 16) & 0xFF;

            if (pci_device_count < PCI_MAX_DEVICES) {
                pci_device_t *d = &pci_devices[pci_device_count++];
                d->bus = 0; d->device = dev; d->function = func;
                d->vendor_id = vendor; d->device_id = device_id;
                d->class_code = class_code; d->subclass = subclass;
                d->prog_if = prog_if; d->header_type = hdr_type;

                serial_printf("[PCI] %02x:%02x.%x vendor=%04x dev=%04x class=%02x:%02x\n",
                    0, dev, func, vendor, device_id, class_code, subclass);
            }

            if (func == 0 && !(hdr_type & 0x80)) break;
        }
    }
    klog(LOG_INFO, "PCI: found %d device(s)", pci_device_count);
}
