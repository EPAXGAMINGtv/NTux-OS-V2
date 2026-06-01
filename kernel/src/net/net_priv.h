#ifndef NTUX_NET_PRIV_H
#define NTUX_NET_PRIV_H

#include <net/net.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    NET_DRV_NONE = 0,
    NET_DRV_VIRTIO,
    NET_DRV_E1000
} net_driver_t;

extern net_driver_t g_net_driver;
extern uint64_t g_net_tx_packets;
extern uint64_t g_net_rx_packets;

uint16_t net_bswap16(uint16_t v);
uint32_t net_bswap32(uint32_t v);
uint32_t net_align_up_u32(uint32_t v, uint32_t align);

uintptr_t net_virt_to_phys(uintptr_t virt);
void* net_phys_to_virt(uintptr_t phys);

bool net_str_starts_with(const char* s, const char* prefix);
void net_append_char(char* out, int* pos, char c, int cap);
void net_append_str(char* out, int* pos, const char* s, int cap);
void net_append_uint(char* out, int* pos, uint32_t v, int cap);
void net_append_ip(char* out, int* pos, uint32_t ip, int cap);

uint16_t net_checksum16(const void* data, uint32_t len);
uint16_t net_checksum_tcp(uint32_t src_ip, uint32_t dst_ip, const uint8_t* tcp, uint16_t tcp_len);

int net_driver_tx_raw(const void* packet, uint16_t packet_len);
void net_driver_poll_rx(void);

void net_stack_reset(void);
void net_process_rx_frame(const uint8_t* frame, uint16_t len);
bool net_wait_until(uint32_t timeout_ms, volatile bool* flag);
int net_debug_dump(char* out, int out_cap);
int net_set_dns_server(uint32_t ip);

#endif



