/* NexOS — kernel/drivers/ata.c | ATA PIO mode disk driver | MIT License */
#include "ata.h"
#include "../kernel.h"

#define ATA_DATA    0x1F0
#define ATA_ERR     0x1F1
#define ATA_SC      0x1F2
#define ATA_LBA_LO  0x1F3
#define ATA_LBA_MID 0x1F4
#define ATA_LBA_HI  0x1F5
#define ATA_DRIVE   0x1F6
#define ATA_STAT    0x1F7
#define ATA_CMD     0x1F7
#define ATA_ALT     0x3F6

#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_READ     0x20
#define ATA_CMD_WRITE    0x30

#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

static int ata_drives[2] = {0, 0};

static void ata_wait(void) {
    /* 400ns delay: read alt status 4 times */
    for (int i = 0; i < 4; i++) io_inb(ATA_ALT);
}

static int ata_poll(void) {
    ata_wait();
    uint8_t st;
    for (int i = 0; i < 100000; i++) {
        st = io_inb(ATA_STAT);
        if (!(st & ATA_SR_BSY)) break;
    }
    if (io_inb(ATA_STAT) & ATA_SR_ERR) return -1;
    return 0;
}

void ata_init(void) {
    for (int drive = 0; drive < 2; drive++) {
        uint16_t buf[256];
        (void)buf;
        /* Select drive */
        io_outb(ATA_DRIVE, (drive == 0) ? 0xA0 : 0xB0);
        ata_wait();

        /* Send IDENTIFY */
        io_outb(ATA_SC, 0); io_outb(ATA_LBA_LO, 0);
        io_outb(ATA_LBA_MID, 0); io_outb(ATA_LBA_HI, 0);
        io_outb(ATA_CMD, ATA_CMD_IDENTIFY);

        uint8_t st = io_inb(ATA_STAT);
        if (st == 0) continue;  /* No drive */

        int timeout = 100000;
        while ((io_inb(ATA_STAT) & ATA_SR_BSY) && timeout-- > 0);

        if (io_inb(ATA_LBA_MID) || io_inb(ATA_LBA_HI)) continue; /* Not ATA */

        while (!(io_inb(ATA_STAT) & (ATA_SR_DRQ | ATA_SR_ERR)));
        if (io_inb(ATA_STAT) & ATA_SR_ERR) continue;

        for (int i = 0; i < 256; i++) {
            buf[i] = io_inw(ATA_DATA);
        }

        ata_drives[drive] = 1;
        klog(LOG_INFO, "ATA: drive %d detected (Primary %s)",
             drive, drive ? "Slave" : "Master");
    }
}

int ata_read_sectors(int drive, uint32_t lba, uint8_t count, void *buf) {
    if (!ata_drives[drive]) return -1;

    io_outb(ATA_DRIVE, (drive == 0 ? 0xE0 : 0xF0) | ((lba >> 24) & 0x0F));
    io_outb(ATA_ERR,   0);
    io_outb(ATA_SC,    count);
    io_outb(ATA_LBA_LO,  (uint8_t)(lba));
    io_outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
    io_outb(ATA_LBA_HI,  (uint8_t)(lba >> 16));
    io_outb(ATA_CMD,   ATA_CMD_READ);

    uint16_t *p = (uint16_t *)buf;
    for (int s = 0; s < count; s++) {
        if (ata_poll() < 0) return -1;
        for (int i = 0; i < 256; i++) p[i] = io_inw(ATA_DATA);
        p += 256;
    }
    return 0;
}

int ata_write_sectors(int drive, uint32_t lba, uint8_t count, const void *buf) {
    if (!ata_drives[drive]) return -1;

    io_outb(ATA_DRIVE, (drive == 0 ? 0xE0 : 0xF0) | ((lba >> 24) & 0x0F));
    io_outb(ATA_ERR,   0);
    io_outb(ATA_SC,    count);
    io_outb(ATA_LBA_LO,  (uint8_t)(lba));
    io_outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
    io_outb(ATA_LBA_HI,  (uint8_t)(lba >> 16));
    io_outb(ATA_CMD,   ATA_CMD_WRITE);

    const uint16_t *p = (const uint16_t *)buf;
    for (int s = 0; s < count; s++) {
        if (ata_poll() < 0) return -1;
        for (int i = 0; i < 256; i++) io_outw(ATA_DATA, p[i]);
        p += 256;
        io_outb(ATA_CMD, 0xE7); /* Flush cache */
        ata_poll();
    }
    return 0;
}
