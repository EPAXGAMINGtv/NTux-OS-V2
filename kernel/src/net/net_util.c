#include <net/net_priv.h>

#include <mm/hhdm.h>
#include <lib/string.h>

uint16_t net_bswap16(uint16_t v) {
    return (uint16_t)((v << 8) | (v >> 8));
}

uint32_t net_bswap32(uint32_t v) {
    return (v << 24) | ((v & 0x0000FF00u) << 8) | ((v & 0x00FF0000u) >> 8) | (v >> 24);
}

uint32_t net_align_up_u32(uint32_t v, uint32_t align) {
    return (v + align - 1u) & ~(align - 1u);
}

uintptr_t net_virt_to_phys(uintptr_t virt) {
    uint64_t off = hhdm_offset_get();
    return (uintptr_t)(virt - off);
}

void* net_phys_to_virt(uintptr_t phys) {
    uint64_t off = hhdm_offset_get();
    return (void*)(uintptr_t)(phys + off);
}

void net_append_char(char* out, int* pos, char c, int cap) {
    if (!out || !pos || *pos >= cap - 1) return;
    out[*pos] = c;
    (*pos)++;
    out[*pos] = '\0';
}

void net_append_str(char* out, int* pos, const char* s, int cap) {
    if (!out || !pos || !s) return;
    for (int i = 0; s[i]; ++i) net_append_char(out, pos, s[i], cap);
}

bool net_str_starts_with(const char* s, const char* prefix) {
    if (!s || !prefix) return false;
    for (size_t i = 0; prefix[i]; ++i) {
        if (s[i] != prefix[i]) return false;
    }
    return true;
}

void net_append_uint(char* out, int* pos, uint32_t v, int cap) {
    char tmp[16];
    int t = 0;
    if (v == 0) {
        net_append_char(out, pos, '0', cap);
        return;
    }
    while (v > 0 && t < (int)sizeof(tmp)) {
        tmp[t++] = (char)('0' + (v % 10));
        v /= 10;
    }
    for (int i = t - 1; i >= 0; --i) net_append_char(out, pos, tmp[i], cap);
}

void net_append_ip(char* out, int* pos, uint32_t ip, int cap) {
    net_append_uint(out, pos, (ip >> 24) & 0xFFu, cap);
    net_append_char(out, pos, '.', cap);
    net_append_uint(out, pos, (ip >> 16) & 0xFFu, cap);
    net_append_char(out, pos, '.', cap);
    net_append_uint(out, pos, (ip >> 8) & 0xFFu, cap);
    net_append_char(out, pos, '.', cap);
    net_append_uint(out, pos, ip & 0xFFu, cap);
}

uint16_t net_checksum16(const void* data, uint32_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += (uint32_t)((p[0] << 8) | p[1]);
        p += 2;
        len -= 2;
    }
    if (len) sum += (uint32_t)(p[0] << 8);
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)(~sum);
}

uint16_t net_checksum_tcp(uint32_t src_ip, uint32_t dst_ip, const uint8_t* tcp, uint16_t tcp_len) {
    uint32_t sum = 0;
    sum += (src_ip >> 16) & 0xFFFFu;
    sum += src_ip & 0xFFFFu;
    sum += (dst_ip >> 16) & 0xFFFFu;
    sum += dst_ip & 0xFFFFu;
    sum += (uint32_t)6; 
    sum += tcp_len;
    const uint8_t* p = tcp;
    uint32_t len = tcp_len;
    while (len > 1) {
        sum += (uint32_t)((p[0] << 8) | p[1]);
        p += 2;
        len -= 2;
    }
    if (len) sum += (uint32_t)(p[0] << 8);
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)(~sum);
}
