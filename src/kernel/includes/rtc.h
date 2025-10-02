#ifndef RTC_H
#define RTC_H

#include <stdint.h>

// RTC registers
#define RTC_SECONDS     0x00
#define RTC_MINUTES     0x02
#define RTC_HOURS       0x04
#define RTC_DAY         0x07
#define RTC_MONTH       0x08
#define RTC_YEAR        0x09

// RTC ports
#define RTC_INDEX_PORT  0x70
#define RTC_DATA_PORT   0x71

// Date/time structure
typedef struct {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} rtc_time_t;

// Timezone structure
typedef struct {
    int8_t offset_hours;    // UTC offset in hours (-12 to +14)
    int8_t offset_minutes;  // Additional minutes offset (usually 0, 15, 30, or 45)
    char name[8];           // Timezone name (e.g., "EST", "PST", "GMT")
} timezone_t;

// Function declarations
void rtc_init(void);
void rtc_read_time(rtc_time_t* time);
void rtc_set_time(rtc_time_t* time);
uint8_t rtc_read_register(uint8_t reg);
void rtc_write_register(uint8_t reg, uint8_t value);
uint8_t bcd_to_binary(uint8_t bcd);
uint8_t binary_to_bcd(uint8_t binary);
void rtc_format_time_string(rtc_time_t* time, char* buffer);
void rtc_format_date_string(rtc_time_t* time, char* buffer);

// Timezone functions
void timezone_init(void);
void timezone_set(int8_t hours_offset, int8_t minutes_offset, const char* name);
void timezone_get(timezone_t* tz);
void rtc_read_local_time(rtc_time_t* time);
int timezone_save_to_disk(void);
int timezone_load_from_disk(void);
void timezone_apply_offset(rtc_time_t* utc_time, rtc_time_t* local_time, timezone_t* tz);

#endif // RTC_H