#ifndef NTUX_NETDRV_H
#define NTUX_NETDRV_H

#include <stdbool.h>
#include <stdint.h>

bool virtio_net_init(void);
int virtio_net_tx_raw(const void* packet, uint16_t packet_len);
void virtio_net_poll_rx(void);

bool e1000_net_init(void);
int e1000_net_tx_raw(const void* packet, uint16_t packet_len);
void e1000_net_poll_rx(void);

#endif
