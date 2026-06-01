#ifndef NTUX_NET_H
#define NTUX_NET_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool ready;
    bool present;
    bool virtio_net_present;
    bool e1000_present;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t bar0;
    uint8_t irq_line;
    uint8_t mac[6];
} net_state_t;

extern net_state_t g_net_state;

void net_init(void);
int net_ping(const char* target, char* out, int out_cap);

#endif
