#ifndef NTUX_BATTERY_H
#define NTUX_BATTERY_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool present;
    bool has_percent;
    uint8_t percent;
    bool charging;
} battery_status_t;

void battery_init(void);
void battery_get_status(battery_status_t* out_status);

#endif
