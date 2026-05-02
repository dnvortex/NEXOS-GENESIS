/* NexOS — kernel/drivers/rtc.c | CMOS RTC via ports 0x70/0x71 | MIT License */
#include "rtc.h"
#include "../kernel.h"

#define RTC_ADDR 0x70
#define RTC_DATA 0x71

static uint8_t rtc_read(uint8_t reg) {
    io_outb(RTC_ADDR, reg);
    return io_inb(RTC_DATA);
}

static int rtc_updating(void) {
    io_outb(RTC_ADDR, 0x0A);
    return io_inb(RTC_DATA) & 0x80;
}

static uint8_t bcd_to_bin(uint8_t bcd) {
    return (bcd & 0x0F) + ((bcd >> 4) * 10);
}

void rtc_get_time(rtc_time_t *t) {
    /* Wait for no update in progress */
    while (rtc_updating());

    uint8_t second = rtc_read(0x00);
    uint8_t minute = rtc_read(0x02);
    uint8_t hour   = rtc_read(0x04);
    uint8_t day    = rtc_read(0x07);
    uint8_t month  = rtc_read(0x08);
    uint8_t year   = rtc_read(0x09);
    uint8_t century = rtc_read(0x32);

    uint8_t regB = rtc_read(0x0B);
    int bcd = !(regB & 0x04);

    if (bcd) {
        second  = bcd_to_bin(second);
        minute  = bcd_to_bin(minute);
        hour    = bcd_to_bin(hour);
        day     = bcd_to_bin(day);
        month   = bcd_to_bin(month);
        year    = bcd_to_bin(year);
        century = bcd_to_bin(century);
    }

    t->second = second;
    t->minute = minute;
    t->hour   = hour;
    t->day    = day;
    t->month  = month;
    t->year   = (century > 0 ? (uint16_t)(century * 100 + year) : (uint16_t)(2000 + year));
}

static void uint_to_str(uint32_t n, char *buf, int width) {
    char tmp[10]; int i = 0;
    do { tmp[i++] = '0' + (int)(n % 10); n /= 10; } while (n);
    while (i < width) tmp[i++] = '0';
    for (int j = 0; j < i; j++) buf[j] = tmp[i-1-j];
    buf[i] = 0;
}

void rtc_time_to_string(char *buf, const rtc_time_t *t) {
    /* Format: YYYY-MM-DD HH:MM:SS */
    char tmp[8];
    int pos = 0;

    uint_to_str(t->year,   tmp, 4); for (int i=0;tmp[i];i++) buf[pos++] = tmp[i];
    buf[pos++] = '-';
    uint_to_str(t->month,  tmp, 2); for (int i=0;tmp[i];i++) buf[pos++] = tmp[i];
    buf[pos++] = '-';
    uint_to_str(t->day,    tmp, 2); for (int i=0;tmp[i];i++) buf[pos++] = tmp[i];
    buf[pos++] = ' ';
    uint_to_str(t->hour,   tmp, 2); for (int i=0;tmp[i];i++) buf[pos++] = tmp[i];
    buf[pos++] = ':';
    uint_to_str(t->minute, tmp, 2); for (int i=0;tmp[i];i++) buf[pos++] = tmp[i];
    buf[pos++] = ':';
    uint_to_str(t->second, tmp, 2); for (int i=0;tmp[i];i++) buf[pos++] = tmp[i];
    buf[pos] = 0;
}
