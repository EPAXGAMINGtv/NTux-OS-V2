#include <net/net_priv.h>

#include <interrupt/timer.h>
#include <lib/string.h>

#define ETH_TYPE_ARP 0x0806
#define ETH_TYPE_IPV4 0x0800
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY 2

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP 6
#define IP_PROTO_UDP 17

#define DNS_PORT 53
#define DNS_MAX_NAME 128

typedef struct __attribute__((packed)) {
    uint8_t dst[6];
    uint8_t src[6];
    uint16_t ethertype;
} eth_hdr_t;

typedef struct __attribute__((packed)) {
    uint16_t htype;
    uint16_t ptype;
    uint8_t hlen;
    uint8_t plen;
    uint16_t oper;
    uint8_t sha[6];
    uint8_t spa[4];
    uint8_t tha[6];
    uint8_t tpa[4];
} arp_pkt_t;

typedef struct __attribute__((packed)) {
    uint8_t ver_ihl;
    uint8_t tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t ttl;
    uint8_t proto;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} ipv4_hdr_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} icmp_hdr_t;

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t len;
    uint16_t checksum;
} udp_hdr_t;

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t data_off;
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urg_ptr;
} tcp_hdr_t;

typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_hdr_t;

typedef struct {
    bool valid;
    uint32_t ip;
    uint8_t mac[6];
} arp_entry_t;

typedef struct {
    bool active;
    uint16_t id;
    uint16_t seq;
    uint32_t expected_src_ip;
    uint8_t reply_ttl;
    bool got;
} icmp_wait_t;

typedef struct {
    bool active;
    uint16_t txid;
    uint16_t src_port;
    uint32_t resolved_ip;
    bool got;
} dns_wait_t;

typedef struct {
    bool active;
    bool connected;
    uint32_t peer_ip;
    uint16_t peer_port;
    uint16_t local_port;
    uint32_t seq;
    uint32_t ack;
    uint32_t peer_seq;
    uint8_t* rx_buf;
    uint32_t rx_cap;
    uint32_t rx_len;
    bool got_fin;
} tcp_conn_t;

static const uint32_t g_ip_addr = 0x0A00020Fu;
static const uint32_t g_netmask = 0xFFFFFF00u;
static const uint32_t g_gateway = 0x0A000202u;
static uint32_t g_dns_server = 0x0A000203u;

static arp_entry_t g_arp_cache[8];
static uint16_t g_next_ip_id = 1;
static uint16_t g_next_icmp_id = 0x4242;
static uint16_t g_next_dns_id = 0x1818;
static uint16_t g_next_src_port = 40000;

static icmp_wait_t g_icmp_wait = {0};
static dns_wait_t g_dns_wait = {0};
static tcp_conn_t g_tcp_conn = {0};

uint64_t g_net_tx_packets = 0;
uint64_t g_net_rx_packets = 0; 

void net_stack_reset(void) {
    memset(g_arp_cache, 0, sizeof(g_arp_cache));
    memset(&g_icmp_wait, 0, sizeof(g_icmp_wait));
    memset(&g_dns_wait, 0, sizeof(g_dns_wait));
    memset(&g_tcp_conn, 0, sizeof(g_tcp_conn));
    g_next_ip_id = 1;
    g_next_icmp_id = 0x4242;
    g_next_dns_id = 0x1818;
    g_next_src_port = 40000;
    g_net_tx_packets = 0;
    g_net_rx_packets = 0;
}


static void append_hex_byte(char* out, int* pos, uint8_t v, int cap) {
    static const char* hex = "0123456789ABCDEF";
    net_append_char(out, pos, hex[(v >> 4) & 0xF], cap);
    net_append_char(out, pos, hex[v & 0xF], cap);
}

static void net_append_mac(char* out, int* pos, const uint8_t mac[6], int cap) {
    for (int i = 0; i < 6; ++i) {
        if (i) net_append_char(out, pos, ':', cap);
        append_hex_byte(out, pos, mac[i], cap);
    }
}

int net_debug_dump(char* out, int out_cap) {
    if (!out || out_cap <= 0) return -1;
    out[0] = '\0';
    int p = 0;

    net_append_str(out, &p, "net: driver=", out_cap);
    if (g_net_driver == NET_DRV_E1000) net_append_str(out, &p, "e1000", out_cap);
    else if (g_net_driver == NET_DRV_VIRTIO) net_append_str(out, &p, "virtio", out_cap);
    else net_append_str(out, &p, "none", out_cap);

    net_append_str(out, &p, " ready=", out_cap);
    net_append_str(out, &p, g_net_state.ready ? "1" : "0", out_cap);
    net_append_str(out, &p, " present=", out_cap);
    net_append_str(out, &p, g_net_state.present ? "1" : "0", out_cap);
    net_append_str(out, &p, " tx=", out_cap);
    net_append_uint(out, &p, (uint32_t)g_net_tx_packets, out_cap);
    net_append_str(out, &p, " rx=", out_cap);
    net_append_uint(out, &p, (uint32_t)g_net_rx_packets, out_cap);
    net_append_char(out, &p, '\n', out_cap);

    net_append_str(out, &p, "mac=", out_cap);
    net_append_mac(out, &p, g_net_state.mac, out_cap);
    net_append_char(out, &p, '\n', out_cap);

    net_append_str(out, &p, "ip=", out_cap);
    net_append_ip(out, &p, g_ip_addr, out_cap);
    net_append_str(out, &p, " netmask=", out_cap);
    net_append_ip(out, &p, g_netmask, out_cap);
    net_append_char(out, &p, '\n', out_cap);

    net_append_str(out, &p, "gw=", out_cap);
    net_append_ip(out, &p, g_gateway, out_cap);
    net_append_str(out, &p, " dns=", out_cap);
    net_append_ip(out, &p, g_dns_server, out_cap);
    net_append_char(out, &p, '\n', out_cap);

    bool any = false;
    for (int i = 0; i < (int)(sizeof(g_arp_cache) / sizeof(g_arp_cache[0])); ++i) {
        if (!g_arp_cache[i].valid) continue;
        any = true;
        net_append_str(out, &p, "arp: ", out_cap);
        net_append_ip(out, &p, g_arp_cache[i].ip, out_cap);
        net_append_str(out, &p, " -> ", out_cap);
        net_append_mac(out, &p, g_arp_cache[i].mac, out_cap);
        net_append_char(out, &p, '\n', out_cap);
    }
    if (!any) {
        net_append_str(out, &p, "arp: (empty)\n", out_cap);
    }
    return 0;
}

int net_set_dns_server(uint32_t ip) {
    if (ip == 0) return -1;
    g_dns_server = ip;
    return 0;
}

static bool in_same_subnet(uint32_t a, uint32_t b);
static bool resolve_arp(uint32_t ip, uint8_t out_mac[6]);

static bool in_same_subnet(uint32_t a, uint32_t b) {
    return (a & g_netmask) == (b & g_netmask);
}

static void arp_store(uint32_t ip, const uint8_t mac[6]) {
    for (int i = 0; i < (int)(sizeof(g_arp_cache) / sizeof(g_arp_cache[0])); ++i) {
        if (g_arp_cache[i].valid && g_arp_cache[i].ip == ip) {
            memcpy(g_arp_cache[i].mac, mac, 6);
            return;
        }
    }
    for (int i = 0; i < (int)(sizeof(g_arp_cache) / sizeof(g_arp_cache[0])); ++i) {
        if (!g_arp_cache[i].valid) {
            g_arp_cache[i].valid = true;
            g_arp_cache[i].ip = ip;
            memcpy(g_arp_cache[i].mac, mac, 6);
            return;
        }
    }
    g_arp_cache[0].valid = true;
    g_arp_cache[0].ip = ip;
    memcpy(g_arp_cache[0].mac, mac, 6);
}

static bool arp_lookup(uint32_t ip, uint8_t mac[6]) {
    for (int i = 0; i < (int)(sizeof(g_arp_cache) / sizeof(g_arp_cache[0])); ++i) {
        if (g_arp_cache[i].valid && g_arp_cache[i].ip == ip) {
            memcpy(mac, g_arp_cache[i].mac, 6);
            return true;
        }
    }
    return false;
}

static int send_arp_request(uint32_t target_ip) {
    uint8_t pkt[sizeof(eth_hdr_t) + sizeof(arp_pkt_t)];
    eth_hdr_t* eth = (eth_hdr_t*)pkt;
    arp_pkt_t* arp = (arp_pkt_t*)(pkt + sizeof(eth_hdr_t));

    memset(eth->dst, 0xFF, 6);
    memcpy(eth->src, g_net_state.mac, 6);
    eth->ethertype = net_bswap16(ETH_TYPE_ARP);

    arp->htype = net_bswap16(1);
    arp->ptype = net_bswap16(ETH_TYPE_IPV4);
    arp->hlen = 6;
    arp->plen = 4;
    arp->oper = net_bswap16(ARP_OP_REQUEST);
    memcpy(arp->sha, g_net_state.mac, 6);
    arp->spa[0] = (uint8_t)(g_ip_addr >> 24);
    arp->spa[1] = (uint8_t)(g_ip_addr >> 16);
    arp->spa[2] = (uint8_t)(g_ip_addr >> 8);
    arp->spa[3] = (uint8_t)g_ip_addr;
    memset(arp->tha, 0x00, 6);
    arp->tpa[0] = (uint8_t)(target_ip >> 24);
    arp->tpa[1] = (uint8_t)(target_ip >> 16);
    arp->tpa[2] = (uint8_t)(target_ip >> 8);
    arp->tpa[3] = (uint8_t)target_ip;

    return net_driver_tx_raw(pkt, (uint16_t)sizeof(pkt));
}

static int send_ipv4_packet(
    const uint8_t dst_mac[6],
    uint32_t dst_ip,
    uint8_t proto,
    const void* l4_payload,
    uint16_t l4_len
) {
    uint8_t pkt[1514];
    if ((uint32_t)sizeof(eth_hdr_t) + sizeof(ipv4_hdr_t) + l4_len > sizeof(pkt)) return -1;

    eth_hdr_t* eth = (eth_hdr_t*)pkt;
    memcpy(eth->dst, dst_mac, 6);
    memcpy(eth->src, g_net_state.mac, 6);
    eth->ethertype = net_bswap16(ETH_TYPE_IPV4);

    ipv4_hdr_t* ip = (ipv4_hdr_t*)(pkt + sizeof(eth_hdr_t));
    memset(ip, 0, sizeof(*ip));
    ip->ver_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = net_bswap16((uint16_t)(sizeof(ipv4_hdr_t) + l4_len));
    ip->id = net_bswap16(g_next_ip_id++);
    ip->flags_frag = net_bswap16(0x4000);
    ip->ttl = 64;
    ip->proto = proto;
    ip->src_ip = net_bswap32(g_ip_addr);
    ip->dst_ip = net_bswap32(dst_ip);
    ip->checksum = 0;
    ip->checksum = net_checksum16(ip, sizeof(*ip));

    memcpy(pkt + sizeof(eth_hdr_t) + sizeof(ipv4_hdr_t), l4_payload, l4_len);
    return net_driver_tx_raw(pkt, (uint16_t)(sizeof(eth_hdr_t) + sizeof(ipv4_hdr_t) + l4_len));
}

static int send_tcp_packet(
    uint32_t dst_ip,
    uint16_t dst_port,
    uint16_t src_port,
    uint32_t seq,
    uint32_t ack,
    uint8_t flags,
    const uint8_t* payload,
    uint16_t payload_len
) {
    uint8_t dst_mac[6];
    uint32_t route_ip = dst_ip;
    if (!in_same_subnet(g_ip_addr, dst_ip)) {
        route_ip = g_gateway;
    }
    if (!resolve_arp(route_ip, dst_mac)) return -1;

    uint8_t pkt[1514];
    uint16_t tcp_len = (uint16_t)(sizeof(tcp_hdr_t) + payload_len);
    if ((uint32_t)sizeof(ipv4_hdr_t) + tcp_len > 1500u) return -1;

    tcp_hdr_t* tcp = (tcp_hdr_t*)pkt;
    memset(tcp, 0, sizeof(*tcp));
    tcp->src_port = net_bswap16(src_port);
    tcp->dst_port = net_bswap16(dst_port);
    tcp->seq = net_bswap32(seq);
    tcp->ack = net_bswap32(ack);
    tcp->data_off = (uint8_t)((sizeof(tcp_hdr_t) / 4) << 4);
    tcp->flags = flags;
    tcp->window = net_bswap16(4096);
    tcp->checksum = 0;
    tcp->urg_ptr = 0;

    if (payload_len && payload) {
        memcpy(pkt + sizeof(tcp_hdr_t), payload, payload_len);
    }
    tcp->checksum = net_checksum_tcp(g_ip_addr, dst_ip, (const uint8_t*)tcp, tcp_len);

    return send_ipv4_packet(dst_mac, dst_ip, IP_PROTO_TCP, pkt, tcp_len);
}

static bool parse_dns_answer_for_a(const uint8_t* pkt, uint16_t len, uint16_t txid, uint32_t* out_ip) {
    if (len < sizeof(dns_hdr_t)) return false;
    const dns_hdr_t* h = (const dns_hdr_t*)pkt;
    if (net_bswap16(h->id) != txid) return false;
    uint16_t qd = net_bswap16(h->qdcount);
    uint16_t an = net_bswap16(h->ancount);
    uint16_t off = sizeof(dns_hdr_t);

    for (uint16_t i = 0; i < qd; ++i) {
        while (off < len && pkt[off] != 0) {
            uint8_t l = pkt[off];
            off += (uint16_t)(l + 1);
            if (off >= len) return false;
        }
        off += 1;
        if (off + 4 > len) return false;
        off += 4;
    }

    for (uint16_t i = 0; i < an; ++i) {
        if (off >= len) return false;
        if ((pkt[off] & 0xC0u) == 0xC0u) {
            off += 2;
        } else {
            while (off < len && pkt[off] != 0) {
                uint8_t l = pkt[off];
                off += (uint16_t)(l + 1);
                if (off >= len) return false;
            }
            off += 1;
        }
        if (off + 10 > len) return false;
        uint16_t type = (uint16_t)((pkt[off] << 8) | pkt[off + 1]);
        uint16_t class_ = (uint16_t)((pkt[off + 2] << 8) | pkt[off + 3]);
        uint16_t rdlen = (uint16_t)((pkt[off + 8] << 8) | pkt[off + 9]);
        off += 10;
        if (off + rdlen > len) return false;
        if (type == 1 && class_ == 1 && rdlen == 4) {
            *out_ip = (uint32_t)pkt[off] << 24 |
                      (uint32_t)pkt[off + 1] << 16 |
                      (uint32_t)pkt[off + 2] << 8 |
                      (uint32_t)pkt[off + 3];
            return true;
        }
        off += rdlen;
    }
    return false;
}

void net_process_rx_frame(const uint8_t* frame, uint16_t len) {
    if (len < sizeof(eth_hdr_t)) return;
    g_net_rx_packets++;
    const eth_hdr_t* eth = (const eth_hdr_t*)frame;
    uint16_t etype = net_bswap16(eth->ethertype);

    if (etype == ETH_TYPE_ARP) {
        if (len < sizeof(eth_hdr_t) + sizeof(arp_pkt_t)) return;
        const arp_pkt_t* arp = (const arp_pkt_t*)(frame + sizeof(eth_hdr_t));
        if (net_bswap16(arp->oper) == ARP_OP_REPLY) {
            uint32_t ip = (uint32_t)arp->spa[0] << 24 |
                          (uint32_t)arp->spa[1] << 16 |
                          (uint32_t)arp->spa[2] << 8 |
                          (uint32_t)arp->spa[3];
            arp_store(ip, arp->sha);
        }
        return;
    }

    if (etype != ETH_TYPE_IPV4) return;
    if (len < sizeof(eth_hdr_t) + sizeof(ipv4_hdr_t)) return;
    const ipv4_hdr_t* ip = (const ipv4_hdr_t*)(frame + sizeof(eth_hdr_t));
    uint8_t ihl = (uint8_t)((ip->ver_ihl & 0x0F) * 4);
    if (ihl < sizeof(ipv4_hdr_t)) return;
    if (len < sizeof(eth_hdr_t) + ihl) return;
    uint16_t total_len = net_bswap16(ip->total_len);
    if (total_len < ihl) return;
    if (sizeof(eth_hdr_t) + total_len > len) return;

    uint32_t src_ip = net_bswap32(ip->src_ip);
    const uint8_t* l4 = frame + sizeof(eth_hdr_t) + ihl;
    uint16_t l4_len = (uint16_t)(total_len - ihl);

    if (ip->proto == IP_PROTO_ICMP && g_icmp_wait.active) {
        if (l4_len < sizeof(icmp_hdr_t)) return;
        const icmp_hdr_t* icmp = (const icmp_hdr_t*)l4;
        if (icmp->type == 0 && icmp->code == 0 &&
            net_bswap16(icmp->id) == g_icmp_wait.id &&
            net_bswap16(icmp->seq) == g_icmp_wait.seq &&
            src_ip == g_icmp_wait.expected_src_ip) {
            g_icmp_wait.reply_ttl = ip->ttl;
            g_icmp_wait.got = true;
        }
    } else if (ip->proto == IP_PROTO_UDP && g_dns_wait.active) {
        if (l4_len < sizeof(udp_hdr_t) + sizeof(dns_hdr_t)) return;
        const udp_hdr_t* udp = (const udp_hdr_t*)l4;
        uint16_t src_port = net_bswap16(udp->src_port);
        uint16_t dst_port = net_bswap16(udp->dst_port);
        if (src_port != DNS_PORT || dst_port != g_dns_wait.src_port) return;
        const uint8_t* dns = l4 + sizeof(udp_hdr_t);
        uint16_t dns_len = (uint16_t)(l4_len - sizeof(udp_hdr_t));
        uint32_t resolved_ip = 0;
        if (parse_dns_answer_for_a(dns, dns_len, g_dns_wait.txid, &resolved_ip)) {
            g_dns_wait.resolved_ip = resolved_ip;
            g_dns_wait.got = true;
        }
    } else if (ip->proto == IP_PROTO_TCP && g_tcp_conn.active) {
        if (l4_len < sizeof(tcp_hdr_t)) return;
        const tcp_hdr_t* tcp = (const tcp_hdr_t*)l4;
        uint16_t src_port = net_bswap16(tcp->src_port);
        uint16_t dst_port = net_bswap16(tcp->dst_port);
        if (dst_port != g_tcp_conn.local_port) return;
        if (src_port != g_tcp_conn.peer_port) return;
        if (src_ip != g_tcp_conn.peer_ip) return;
        uint8_t flags = tcp->flags;
        uint32_t seq = net_bswap32(tcp->seq);
        uint32_t ack = net_bswap32(tcp->ack);
        uint8_t data_off = (uint8_t)((tcp->data_off >> 4) * 4);
        if (data_off < sizeof(tcp_hdr_t)) return;
        if (l4_len < data_off) return;
        const uint8_t* payload = l4 + data_off;
        uint16_t plen = (uint16_t)(l4_len - data_off);

        if ((flags & 0x12) == 0x12) {
            g_tcp_conn.peer_seq = seq;
            g_tcp_conn.ack = seq + 1;
            g_tcp_conn.connected = true;
        } else if (flags & 0x10) {
            (void)ack;
        }
        if (plen > 0 && g_tcp_conn.rx_buf && g_tcp_conn.rx_len < g_tcp_conn.rx_cap) {
            uint32_t copy = plen;
            if (g_tcp_conn.rx_len + copy > g_tcp_conn.rx_cap) {
                copy = g_tcp_conn.rx_cap - g_tcp_conn.rx_len;
            }
            if (copy > 0) {
                memcpy(g_tcp_conn.rx_buf + g_tcp_conn.rx_len, payload, copy);
                g_tcp_conn.rx_len += copy;
                g_tcp_conn.ack = seq + plen;
            }
        }
        if (flags & 0x01) {
            g_tcp_conn.got_fin = true;
            g_tcp_conn.ack = seq + 1;
        }
        (void)send_tcp_packet(g_tcp_conn.peer_ip, g_tcp_conn.peer_port, g_tcp_conn.local_port,
                              g_tcp_conn.seq, g_tcp_conn.ack, 0x10, 0, 0);
    }
}

bool net_wait_until(uint32_t timeout_ms, volatile bool* flag) {
    uint64_t start = get_tick_count();
    uint32_t ticks = (timeout_ms * timer_get_hz()) / 1000u + 1u;
    while (!*flag) {
        net_driver_poll_rx();
        if ((int64_t)(get_tick_count() - start) > (int64_t)ticks) return false;
    }
    return true;
}

static bool resolve_arp(uint32_t ip, uint8_t out_mac[6]) {
    if (arp_lookup(ip, out_mac)) return true;
    if (send_arp_request(ip) != 0) return false;

    uint64_t start = get_tick_count();
    uint32_t ticks = timer_get_hz() * 2u;
    while ((int64_t)(get_tick_count() - start) <= (int64_t)ticks) {
        net_driver_poll_rx();
        if (arp_lookup(ip, out_mac)) return true;
    }
    return false;
}

static bool tcp_connect(uint32_t dst_ip, uint16_t dst_port, uint8_t* rx_buf, uint32_t rx_cap) {
    if (g_tcp_conn.active) return false;
    memset(&g_tcp_conn, 0, sizeof(g_tcp_conn));
    g_tcp_conn.active = true;
    g_tcp_conn.peer_ip = dst_ip;
    g_tcp_conn.peer_port = dst_port;
    g_tcp_conn.local_port = g_next_src_port++;
    g_tcp_conn.seq = (uint32_t)get_tick_count();
    g_tcp_conn.ack = 0;
    g_tcp_conn.rx_buf = rx_buf;
    g_tcp_conn.rx_cap = rx_cap;
    g_tcp_conn.rx_len = 0;

    if (send_tcp_packet(dst_ip, dst_port, g_tcp_conn.local_port, g_tcp_conn.seq, 0, 0x02, 0, 0) != 0) {
        g_tcp_conn.active = false;
        return false;
    }

    if (!net_wait_until(1500, &g_tcp_conn.connected)) {
        g_tcp_conn.active = false;
        return false;
    }

    g_tcp_conn.seq += 1;
    (void)send_tcp_packet(dst_ip, dst_port, g_tcp_conn.local_port, g_tcp_conn.seq, g_tcp_conn.ack, 0x10, 0, 0);
    return true;
}

static bool tcp_send_and_recv(uint32_t dst_ip, uint16_t dst_port, const uint8_t* payload, uint16_t len) {
    if (!g_tcp_conn.connected) return false;
    if (send_tcp_packet(dst_ip, dst_port, g_tcp_conn.local_port, g_tcp_conn.seq, g_tcp_conn.ack, 0x18, payload, len) != 0) {
        return false;
    }
    g_tcp_conn.seq += len;
    uint64_t start = get_tick_count();
    uint32_t ticks = timer_get_hz() * 4u;
    while (!g_tcp_conn.got_fin) {
        net_driver_poll_rx();
        if ((int64_t)(get_tick_count() - start) > (int64_t)ticks) break;
    }
    return g_tcp_conn.rx_len > 0;
}

static bool build_dns_query(const char* host, uint8_t* out, uint16_t* out_len, uint16_t txid) {
    if (!host || !host[0]) return false;
    uint16_t p = 0;
    dns_hdr_t* h = (dns_hdr_t*)out;
    h->id = net_bswap16(txid);
    h->flags = net_bswap16(0x0100);
    h->qdcount = net_bswap16(1);
    h->ancount = 0;
    h->nscount = 0;
    h->arcount = 0;
    p += sizeof(dns_hdr_t);

    int label_len = 0;
    uint16_t label_pos = p++;
    for (int i = 0;; ++i) {
        char c = host[i];
        if (c == '.' || c == '\0') {
            out[label_pos] = (uint8_t)label_len;
            if (c == '\0') break;
            label_len = 0;
            label_pos = p++;
            continue;
        }
        if (label_len >= 63 || p >= 500) return false;
        out[p++] = (uint8_t)c;
        label_len++;
    }
    out[p++] = 0;
    out[p++] = 0; out[p++] = 1;
    out[p++] = 0; out[p++] = 1;
    *out_len = p;
    return true;
}

static bool send_dns_query(const char* host, uint32_t* out_ip) {
    uint8_t dst_mac[6];
    if (!resolve_arp(g_dns_server, dst_mac)) return false;

    uint8_t dns_query[512];
    uint16_t dns_len = 0;
    uint16_t txid = g_next_dns_id++;
    if (!build_dns_query(host, dns_query, &dns_len, txid)) return false;

    uint8_t udp_payload[600];
    udp_hdr_t* udp = (udp_hdr_t*)udp_payload;
    uint16_t src_port = g_next_src_port++;
    if (g_next_src_port < 40000) g_next_src_port = 40000;

    udp->src_port = net_bswap16(src_port);
    udp->dst_port = net_bswap16(DNS_PORT);
    udp->len = net_bswap16((uint16_t)(sizeof(udp_hdr_t) + dns_len));
    udp->checksum = 0;
    memcpy(udp_payload + sizeof(udp_hdr_t), dns_query, dns_len);

    g_dns_wait.active = true;
    g_dns_wait.txid = txid;
    g_dns_wait.src_port = src_port;
    g_dns_wait.got = false;
    g_dns_wait.resolved_ip = 0;

    if (send_ipv4_packet(dst_mac, g_dns_server, IP_PROTO_UDP, udp_payload, (uint16_t)(sizeof(udp_hdr_t) + dns_len)) != 0) {
        g_dns_wait.active = false;
        return false;
    }

    bool ok = net_wait_until(1500, &g_dns_wait.got);
    if (ok && out_ip) *out_ip = g_dns_wait.resolved_ip;
    g_dns_wait.active = false;
    return ok;
}

static bool is_ipv4_literal(const char* s, uint32_t* out_ip) {
    if (!s || !s[0]) return false;
    uint32_t parts[4] = {0};
    int pi = 0;
    for (int i = 0;; ++i) {
        char c = s[i];
        if (c == '.' || c == '\0') {
            if (pi > 3) return false;
            if (parts[pi] > 255) return false;
            if (c == '\0') break;
            pi++;
            continue;
        }
        if (c < '0' || c > '9') return false;
        parts[pi] = parts[pi] * 10u + (uint32_t)(c - '0');
        if (parts[pi] > 255) return false;
    }
    if (pi != 3) return false;
    if (out_ip) {
        *out_ip = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    }
    return true;
}

static bool resolve_target(const char* target, uint32_t* out_ip) {
    if (!target || !target[0]) return false;
    if (strcmp(target, "self") == 0 || strcmp(target, "me") == 0 || strcmp(target, "myself") == 0) {
        *out_ip = g_ip_addr;
        return true;
    }
    if (strcmp(target, "localhost") == 0) {
        *out_ip = 0x7F000001u;
        return true;
    }
    if (is_ipv4_literal(target, out_ip)) return true;
    return send_dns_query(target, out_ip);
}

static bool ping_once(uint32_t dst_ip, uint16_t id, uint16_t seq, uint32_t* out_rtt_ms, uint8_t* out_ttl) {
    uint8_t dst_mac[6];
    uint32_t next_hop = in_same_subnet(g_ip_addr, dst_ip) ? dst_ip : g_gateway;
    if (!resolve_arp(next_hop, dst_mac)) return false;

    uint8_t payload[32];
    for (int i = 0; i < (int)sizeof(payload); ++i) payload[i] = (uint8_t)(i + 1);

    uint8_t icmp_packet[sizeof(icmp_hdr_t) + sizeof(payload)];
    icmp_hdr_t* icmp = (icmp_hdr_t*)icmp_packet;
    icmp->type = 8;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->id = net_bswap16(id);
    icmp->seq = net_bswap16(seq);
    memcpy(icmp_packet + sizeof(icmp_hdr_t), payload, sizeof(payload));
    icmp->checksum = net_checksum16(icmp_packet, sizeof(icmp_packet));

    g_icmp_wait.active = true;
    g_icmp_wait.id = id;
    g_icmp_wait.seq = seq;
    g_icmp_wait.expected_src_ip = dst_ip;
    g_icmp_wait.got = false;
    g_icmp_wait.reply_ttl = 0;

    uint64_t start = get_tick_count();
    if (send_ipv4_packet(dst_mac, dst_ip, IP_PROTO_ICMP, icmp_packet, sizeof(icmp_packet)) != 0) {
        g_icmp_wait.active = false;
        return false;
    }

    bool ok = net_wait_until(1000, &g_icmp_wait.got);
    if (ok) {
        uint64_t dt = get_tick_count() - start;
        *out_rtt_ms = (uint32_t)((dt * 1000u) / timer_get_hz());
        *out_ttl = g_icmp_wait.reply_ttl;
    }
    g_icmp_wait.active = false;
    return ok;
}

int net_ping(const char* target, char* out, int out_cap) {
    if (!out || out_cap <= 0) return -1;
    out[0] = '\0';
    int p = 0;

    if (!target || !target[0]) {
        net_append_str(out, &p, "Usage: ping <host>", out_cap);
        return -1;
    }
    if (!g_net_state.ready) {
        net_append_str(out, &p, "ping: network is not initialized", out_cap);
        return -1;
    }

    uint32_t dst_ip = 0;
    if (!resolve_target(target, &dst_ip)) {
        net_append_str(out, &p, "ping: cannot resolve host '", out_cap);
        net_append_str(out, &p, target, out_cap);
        net_append_str(out, &p, "'", out_cap);
        return -1;
    }

    net_append_str(out, &p, "PING ", out_cap);
    net_append_str(out, &p, target, out_cap);
    net_append_str(out, &p, " (", out_cap);
    net_append_ip(out, &p, dst_ip, out_cap);
    net_append_str(out, &p, "): 32 data bytes\n", out_cap);

    uint16_t id = g_next_icmp_id++;
    int received = 0;
    for (uint16_t seq = 1; seq <= 4; ++seq) {
        uint32_t rtt = 0;
        uint8_t ttl = 0;
        if (ping_once(dst_ip, id, seq, &rtt, &ttl)) {
            net_append_str(out, &p, "Reply from ", out_cap);
            net_append_ip(out, &p, dst_ip, out_cap);
            net_append_str(out, &p, ": bytes=32 time=", out_cap);
            net_append_uint(out, &p, rtt, out_cap);
            net_append_str(out, &p, "ms TTL=", out_cap);
            net_append_uint(out, &p, ttl, out_cap);
            net_append_char(out, &p, '\n', out_cap);
            received++;
        } else {
            net_append_str(out, &p, "Request timeout for icmp_seq=", out_cap);
            net_append_uint(out, &p, seq, out_cap);
            net_append_char(out, &p, '\n', out_cap);
        }
    }

    net_append_str(out, &p, "Ping statistics: sent=4 received=", out_cap);
    net_append_uint(out, &p, (uint32_t)received, out_cap);
    net_append_str(out, &p, " lost=", out_cap);
    net_append_uint(out, &p, (uint32_t)(4 - received), out_cap);
    return (received > 0) ? 0 : -1;
}

int net_http_get(const char* url, char* out, int out_cap) {
    if (!out || out_cap <= 0) return -1;
    out[0] = '\0';
    if (!url || !url[0]) return -1;
    if (net_str_starts_with(url, "https://")) {
        net_append_str(out, &(int){0}, "HTTPS not supported yet", out_cap);
        return -1;
    }
    const char* p = url;
    if (net_str_starts_with(url, "http://")) {
        p += 7;
    }
    char host[128] = "";
    char path[256] = "/";
    int hi = 0;
    while (*p && *p != '/' && hi < (int)sizeof(host) - 1) {
        host[hi++] = *p++;
    }
    host[hi] = '\0';
    if (*p == '/') {
        strncpy(path, p, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    }
    if (!host[0]) return -1;

    uint32_t dst_ip = 0;
    if (!resolve_target(host, &dst_ip)) return -1;

    if (!tcp_connect(dst_ip, 80, (uint8_t*)out, (uint32_t)out_cap - 1)) return -1;
    char req[512];
    int n = 0;
    req[0] = '\0';
    net_append_str(req, &n, "GET ", (int)sizeof(req));
    net_append_str(req, &n, path, (int)sizeof(req));
    net_append_str(req, &n, " HTTP/1.0\r\nHost: ", (int)sizeof(req));
    net_append_str(req, &n, host, (int)sizeof(req));
    net_append_str(req, &n, "\r\nUser-Agent: NTuxBrowser\r\nConnection: close\r\n\r\n", (int)sizeof(req));

    if (!tcp_send_and_recv(dst_ip, 80, (const uint8_t*)req, (uint16_t)n)) return -1;
    if (g_tcp_conn.rx_len >= (uint32_t)out_cap) g_tcp_conn.rx_len = (uint32_t)out_cap - 1u;
    out[g_tcp_conn.rx_len] = '\0';
    g_tcp_conn.active = false;
    return 0;
}















