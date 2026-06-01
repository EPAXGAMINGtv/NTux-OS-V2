#include "cmos.h"
#include <arch/x86_64/io.h>
#include <drivers/framebuffer/kprint.h>
#include <lib/string.h>




uint8_t cmos_read_register(uint8_t reg) {
    
    uint8_t flags = 0;  
    
    
    outb(CMOS_PORT_ADDR, reg | 0x80);
    
    
    uint8_t value = inb(CMOS_PORT_DATA);
    
    return value;
}


void cmos_write_register(uint8_t reg, uint8_t value) {
    
    uint8_t flags = 0;
    
    
    outb(CMOS_PORT_ADDR, reg | 0x80);
    
    
    outb(CMOS_PORT_DATA, value);
}


bool cmos_is_updating(void) {
    
    for (int i = 0; i < 100000; i++) {
        uint8_t status = cmos_read_register(CMOS_REG_STATUS_A);
        if (!(status & 0x80)) {
            return false;
        }
    }
    return true;
}


bool cmos_init(void) {
    kprint("[CMOS] Initializing CMOS RTC...\n");
    
    
    uint8_t reg_a = cmos_read_register(CMOS_REG_STATUS_A);
    uint8_t reg_b = cmos_read_register(CMOS_REG_STATUS_B);
    
    kprint("[CMOS] Status A: 0x");
    kprinthex(reg_a);
    kprint(", Status B: 0x");
    kprinthex(reg_b);
    kprint("\n");
    
    
    if ((reg_a & 0x80) == 0) {
        kprint("[CMOS] WARNING: RTC not updating - may not be present\n");
    }
    
    
    uint8_t reg_b_new = reg_b | 0x04;  
    cmos_write_register(CMOS_REG_STATUS_B, reg_b_new);
    
    kprint("[CMOS] CMOS RTC initialized\n");
    return true;
}


bool cmos_read_time(cmos_time_t* time) {
    if (!time) {
        return false;
    }
    
    
    while (cmos_is_updating()) {
        
    }
    
    
    uint8_t reg_b = cmos_read_register(CMOS_REG_STATUS_B);
    bool binary_mode = (reg_b & 0x04) != 0;
    bool hour_24h = (reg_b & 0x02) == 0;
    
    
    time->second = cmos_read_register(CMOS_REG_SECONDS);
    time->minute = cmos_read_register(CMOS_REG_MINUTES);
    time->hour = cmos_read_register(CMOS_REG_HOURS);
    time->day = cmos_read_register(CMOS_REG_DAY);
    time->month = cmos_read_register(CMOS_REG_MONTH);
    time->year = cmos_read_register(CMOS_REG_YEAR);
    
    
    uint8_t century = cmos_read_register(CMOS_REG_CENTURY);
    if (century != 0 && century != 0xFF) {
        time->year += (century * 100);
    } else {
        
        time->year += 2000;
    }
    
    
    if (!binary_mode) {
        time->second = cmos_bcd_to_binary(time->second);
        time->minute = cmos_bcd_to_binary(time->minute);
        time->hour = cmos_bcd_to_binary(time->hour);
        time->day = cmos_bcd_to_binary(time->day);
        time->month = cmos_bcd_to_binary(time->month);
        time->year = cmos_bcd_to_binary(time->year % 100);
        
        
        if (century != 0 && century != 0xFF) {
            time->year += (cmos_bcd_to_binary(century) * 100);
        } else if (time->year < 2000) {
            time->year += 2000;
        }
    }
    
    
    if (!hour_24h && (time->hour & 0x80)) {
        time->hour = (time->hour & 0x7F) + 12;
    }
    
    return true;
}


uint64_t cmos_get_unix_time(void) {
    cmos_time_t time;
    
    if (!cmos_read_time(&time)) {
        return 0;
    }
    
    
    
    uint16_t days = 0;
    
    
    for (uint16_t y = 1970; y < time.year; y++) {
        
        if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) {
            days += 366;
        } else {
            days += 365;
        }
    }
    
    
    static const uint8_t days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (uint8_t m = 1; m < time.month; m++) {
        days += days_in_month[m - 1];
        
        if (m == 2 && ((time.year % 4 == 0 && time.year % 100 != 0) || (time.year % 400 == 0))) {
            days += 1;
        }
    }
    
    
    days += time.day - 1;
    
    
    uint64_t seconds = (uint64_t)days * 86400;  
    seconds += (uint64_t)time.hour * 3600;
    seconds += (uint64_t)time.minute * 60;
    seconds += time.second;
    
    return seconds;
}
