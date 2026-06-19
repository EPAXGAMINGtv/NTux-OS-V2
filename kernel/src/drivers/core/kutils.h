#ifndef CORE_KUTILS_H
#define CORE_KUTILS_H

#include <lib/kutils.h>
#include <stdint.h>

static inline void serial_write_hex(uint32_t val) {
    char buf[11] = "0x";
    for (int i = 7; i >= 0; i--) {
        uint8_t nib = (uint8_t)((val >> (i * 4)) & 0xF);
        buf[9 - i] = nib < 10 ? '0' + nib : 'a' + nib - 10;
    }
    buf[10] = '\0';
    kprint_serial_only(buf);
}

#endif
