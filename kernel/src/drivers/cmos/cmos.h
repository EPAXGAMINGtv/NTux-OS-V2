#ifndef CMOS_H
#define CMOS_H

#include <stdint.h>
#include <stdbool.h>


#define CMOS_REG_SECONDS     0x00
#define CMOS_REG_MINUTES     0x02
#define CMOS_REG_HOURS       0x04
#define CMOS_REG_DAY         0x07
#define CMOS_REG_MONTH       0x08
#define CMOS_REG_YEAR        0x09
#define CMOS_REG_CENTURY     0x32  
#define CMOS_REG_STATUS_A    0x0A
#define CMOS_REG_STATUS_B    0x0B
#define CMOS_REG_STATUS_C    0x0C
#define CMOS_REG_STATUS_D    0x0D


#define CMOS_PORT_ADDR       0x70
#define CMOS_PORT_DATA       0x71


typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} cmos_time_t;


bool cmos_init(void);


bool cmos_read_time(cmos_time_t* time);


uint8_t cmos_read_register(uint8_t reg);


void cmos_write_register(uint8_t reg, uint8_t value);


bool cmos_is_updating(void);


uint64_t cmos_get_unix_time(void);


static inline uint8_t cmos_bcd_to_binary(uint8_t bcd) {
    return ((bcd & 0xF0) >> 1) + ((bcd & 0xF0) >> 3) + (bcd & 0x0F);
}


static inline uint8_t cmos_binary_to_bcd(uint8_t binary) {
    return ((binary / 10) << 4) | (binary % 10);
}

#endif 
