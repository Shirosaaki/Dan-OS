//
// Created by Shirosaaki on 02/10/2025.
//

#include <kernel/drivers/rtc.h>
#include "../../cpu/ports.h"
#include <kernel/sys/tty.h>
#include <kernel/fs/fat32.h>

// Global timezone settings
static timezone_t current_timezone = {0, 0, "UTC"};  // Default to UTC

// Initialize the RTC
void rtc_init(void) {
    // Disable NMI and select status register A
    outb(RTC_INDEX_PORT, 0x8A);
    
    // Read current value of register A
    uint8_t status_a = inb(RTC_DATA_PORT);
    
    // Set the rate to 1024 Hz (default)
    outb(RTC_INDEX_PORT, 0x8A);
    outb(RTC_DATA_PORT, (status_a & 0xF0) | 0x06);
    
    // Enable RTC updates
    outb(RTC_INDEX_PORT, 0x8B);
    uint8_t status_b = inb(RTC_DATA_PORT);
    outb(RTC_INDEX_PORT, 0x8B);
    outb(RTC_DATA_PORT, status_b | 0x02); // 24-hour format
}

// Read a register from the RTC
uint8_t rtc_read_register(uint8_t reg) {
    outb(RTC_INDEX_PORT, reg | 0x80); // Disable NMI
    return inb(RTC_DATA_PORT);
}

// Write a register to the RTC
void rtc_write_register(uint8_t reg, uint8_t value) {
    outb(RTC_INDEX_PORT, reg | 0x80); // Disable NMI
    outb(RTC_DATA_PORT, value);
}

// Convert BCD to binary
uint8_t bcd_to_binary(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

// Convert binary to BCD
uint8_t binary_to_bcd(uint8_t binary) {
    return ((binary / 10) << 4) | (binary % 10);
}

// Read current time from RTC
void rtc_read_time(rtc_time_t* time) {
    uint8_t status_b;
    
    // Make sure we don't read during an update
    while (rtc_read_register(0x0A) & 0x80);
    
    // Read time values
    time->seconds = rtc_read_register(RTC_SECONDS);
    time->minutes = rtc_read_register(RTC_MINUTES);
    time->hours = rtc_read_register(RTC_HOURS);
    time->day = rtc_read_register(RTC_DAY);
    time->month = rtc_read_register(RTC_MONTH);
    time->year = rtc_read_register(RTC_YEAR);
    
    // Check if we need to convert from BCD
    status_b = rtc_read_register(0x0B);
    if (!(status_b & 0x04)) {
        // Values are in BCD, convert to binary
        time->seconds = bcd_to_binary(time->seconds);
        time->minutes = bcd_to_binary(time->minutes);
        time->hours = bcd_to_binary(time->hours);
        time->day = bcd_to_binary(time->day);
        time->month = bcd_to_binary(time->month);
        time->year = bcd_to_binary(time->year);
    }
    
    // Convert 2-digit year to full year (assume 21st century for now)
    if (time->year < 80) {
        time->year += 2000;
    } else {
        time->year += 1900;
    }
}

// Set RTC time
void rtc_set_time(rtc_time_t* time) {
    uint8_t status_b = rtc_read_register(0x0B);
    uint8_t seconds, minutes, hours, day, month, year;
    
    // Convert year back to 2-digit format
    if (time->year >= 2000) {
        year = time->year - 2000;
    } else {
        year = time->year - 1900;
    }
    
    // Convert to BCD if necessary
    if (!(status_b & 0x04)) {
        seconds = binary_to_bcd(time->seconds);
        minutes = binary_to_bcd(time->minutes);
        hours = binary_to_bcd(time->hours);
        day = binary_to_bcd(time->day);
        month = binary_to_bcd(time->month);
        year = binary_to_bcd(year);
    } else {
        seconds = time->seconds;
        minutes = time->minutes;
        hours = time->hours;
        day = time->day;
        month = time->month;
    }
    
    // Disable updates while setting time
    status_b = rtc_read_register(0x0B);
    rtc_write_register(0x0B, status_b | 0x80);
    
    // Set the time
    rtc_write_register(RTC_SECONDS, seconds);
    rtc_write_register(RTC_MINUTES, minutes);
    rtc_write_register(RTC_HOURS, hours);
    rtc_write_register(RTC_DAY, day);
    rtc_write_register(RTC_MONTH, month);
    rtc_write_register(RTC_YEAR, year);
    
    // Re-enable updates
    rtc_write_register(0x0B, status_b & ~0x80);
}

// Format time as HH:MM:SS
void rtc_format_time_string(rtc_time_t* time, char* buffer) {
    // Simple number to string conversion for time
    buffer[0] = '0' + (time->hours / 10);
    buffer[1] = '0' + (time->hours % 10);
    buffer[2] = ':';
    buffer[3] = '0' + (time->minutes / 10);
    buffer[4] = '0' + (time->minutes % 10);
    buffer[5] = ':';
    buffer[6] = '0' + (time->seconds / 10);
    buffer[7] = '0' + (time->seconds % 10);
    buffer[8] = '\0';
}

// Format date as DD/MM/YYYY
void rtc_format_date_string(rtc_time_t* time, char* buffer) {
    // DD/MM/YYYY format
    buffer[0] = '0' + (time->day / 10);
    buffer[1] = '0' + (time->day % 10);
    buffer[2] = '/';
    buffer[3] = '0' + (time->month / 10);
    buffer[4] = '0' + (time->month % 10);
    buffer[5] = '/';
    
    // Year (YYYY)
    uint16_t year = time->year;
    buffer[6] = '0' + (year / 1000);
    buffer[7] = '0' + ((year / 100) % 10);
    buffer[8] = '0' + ((year / 10) % 10);
    buffer[9] = '0' + (year % 10);
    buffer[10] = '\0';
}

// Initialize timezone system
void timezone_init(void) {
    // Try to load timezone from disk, default to UTC if not found
    if (timezone_load_from_disk() != 0) {
        // Default to UTC if no saved timezone
        current_timezone.offset_hours = 0;
        current_timezone.offset_minutes = 0;
        current_timezone.name[0] = 'U';
        current_timezone.name[1] = 'T';
        current_timezone.name[2] = 'C';
        current_timezone.name[3] = '\0';
    }
}

// Set timezone
void timezone_set(int8_t hours_offset, int8_t minutes_offset, const char* name) {
    current_timezone.offset_hours = hours_offset;
    current_timezone.offset_minutes = minutes_offset;
    
    // Copy name (max 7 chars + null terminator)
    int i = 0;
    while (name[i] && i < 7) {
        current_timezone.name[i] = name[i];
        i++;
    }
    current_timezone.name[i] = '\0';
    
    // Save to disk
    timezone_save_to_disk();
}

// Get current timezone
void timezone_get(timezone_t* tz) {
    tz->offset_hours = current_timezone.offset_hours;
    tz->offset_minutes = current_timezone.offset_minutes;
    
    int i = 0;
    while (current_timezone.name[i] && i < 7) {
        tz->name[i] = current_timezone.name[i];
        i++;
    }
    tz->name[i] = '\0';
}

// Apply timezone offset to convert UTC to local time
void timezone_apply_offset(rtc_time_t* utc_time, rtc_time_t* local_time, timezone_t* tz) {
    // Copy UTC time
    *local_time = *utc_time;
    
    // Add timezone offset
    int total_minutes = local_time->minutes + tz->offset_minutes;
    int total_hours = local_time->hours + tz->offset_hours;
    
    // Handle minute overflow/underflow
    if (total_minutes >= 60) {
        total_minutes -= 60;
        total_hours++;
    } else if (total_minutes < 0) {
        total_minutes += 60;
        total_hours--;
    }
    
    local_time->minutes = total_minutes;
    
    // Handle hour overflow/underflow (simplified - doesn't handle date changes)
    if (total_hours >= 24) {
        local_time->hours = total_hours - 24;
        // TODO: Increment day (would need calendar logic)
    } else if (total_hours < 0) {
        local_time->hours = total_hours + 24;
        // TODO: Decrement day (would need calendar logic)
    } else {
        local_time->hours = total_hours;
    }
}

// Read local time (UTC + timezone offset)
void rtc_read_local_time(rtc_time_t* time) {
    rtc_time_t utc_time;
    rtc_read_time(&utc_time);
    timezone_apply_offset(&utc_time, time, &current_timezone);
}

// Save timezone to disk (stores in a special file)
int timezone_save_to_disk(void) {
    // Create a small data structure to save
    uint8_t tz_data[16];
    tz_data[0] = current_timezone.offset_hours;
    tz_data[1] = current_timezone.offset_minutes;
    
    // Copy name
    for (int i = 0; i < 8; i++) {
        tz_data[2 + i] = current_timezone.name[i];
    }
    
    // Save to a hidden system file
    return fat32_create_file(".timezone", tz_data, 10);
}

// Load timezone from disk
int timezone_load_from_disk(void) {
    fat32_file_t tz_file;
    
    if (fat32_open_file(".timezone", &tz_file) == 0) {
        uint8_t tz_data[16];
        int bytes_read = fat32_read_file(&tz_file, tz_data, 10);
        
        if (bytes_read >= 10) {
            current_timezone.offset_hours = (int8_t)tz_data[0];
            current_timezone.offset_minutes = (int8_t)tz_data[1];
            
            // Copy name
            for (int i = 0; i < 8; i++) {
                current_timezone.name[i] = tz_data[2 + i];
            }
            current_timezone.name[7] = '\0';  // Ensure null termination
            
            return 0;  // Success
        }
    }
    
    return -1;  // Failed to load
}