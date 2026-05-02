/* NexOS — kernel/drivers/rtc.h | Real-Time Clock | MIT License */
#ifndef RTC_H
#define RTC_H

#include <stdint.h>

typedef struct {
    uint8_t second, minute, hour;
    uint8_t day, month;
    uint16_t year;
} rtc_time_t;

void rtc_get_time(rtc_time_t *t);
void rtc_time_to_string(char *buf, const rtc_time_t *t);

#endif
