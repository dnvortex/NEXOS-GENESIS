/* NexOS — kernel/drivers/ata.h | ATA/IDE disk driver | MIT License */
#ifndef ATA_H
#define ATA_H

#include <stdint.h>

#define ATA_PRIMARY_MASTER 0
#define ATA_PRIMARY_SLAVE  1

void ata_init(void);
int  ata_read_sectors(int drive, uint32_t lba, uint8_t count, void *buf);
int  ata_write_sectors(int drive, uint32_t lba, uint8_t count, const void *buf);

#endif
