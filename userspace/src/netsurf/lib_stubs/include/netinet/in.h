#ifndef _NETINET_IN_H
#define _NETINET_IN_H

#include <stdint.h>
#include <sys/socket.h>

typedef uint32_t in_addr_t;

struct in_addr {
    in_addr_t s_addr;
};

struct in6_addr {
    uint8_t s6_addr[16];
};

#define IPPROTO_IP 0
#define IPPROTO_ICMP 1
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define IPPROTO_IPV6 41

#define INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46

#endif
