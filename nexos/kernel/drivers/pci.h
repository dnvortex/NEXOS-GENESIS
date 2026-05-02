/* NexOS — kernel/drivers/pci.h | PCI bus enumeration | MIT License */
#ifndef PCI_H
#define PCI_H

#include <stdint.h>

typedef struct {
    uint8_t  bus, device, function;
    uint16_t vendor_id, device_id;
    uint8_t  class_code, subclass, prog_if;
    uint8_t  header_type;
} pci_device_t;

#define PCI_MAX_DEVICES 128

extern pci_device_t pci_devices[];
extern int pci_device_count;

void     pci_init(void);
uint32_t pci_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
void     pci_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val);

#endif
