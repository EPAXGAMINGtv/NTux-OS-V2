#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define AF_UNSPEC 0
#define AF_UNIX 1
#define AF_INET 2
#define AF_INET6 10

#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_RAW 3
#define SOCK_NONBLOCK 0x4000
#define SOCK_CLOEXEC 0x8000

#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define IPPROTO_IP 0

#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define SO_KEEPALIVE 9
#define SO_BROADCAST 6
#define SO_SNDBUF 7
#define SO_RCVBUF 8
#define SO_ERROR 4
#define SO_TYPE 3
#define SO_LINGER 13
#define SO_RCVTIMEO 20
#define SO_SNDTIMEO 21
#define SO_REUSEPORT 15

#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2

#define MSG_NOSIGNAL 0x4000
#define MSG_PEEK 2
#define MSG_WAITALL 0x100
#define MSG_DONTWAIT 0x40

#define INADDR_ANY ((uint32_t)0x00000000)
#define INADDR_LOOPBACK ((uint32_t)0x7f000001)
#define INADDR_BROADCAST ((uint32_t)0xffffffff)
#define INADDR_NONE ((uint32_t)0xffffffff)

struct sockaddr {
    uint16_t sa_family;
    char sa_data[14];
};

struct sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    char sin_zero[8];
};

struct sockaddr_in6 {
    uint16_t sin6_family;
    uint16_t sin6_port;
    uint32_t sin6_flowinfo;
    uint8_t sin6_addr[16];
    uint32_t sin6_scope_id;
};

struct sockaddr_storage {
    uint16_t ss_family;
    char __data[126];
};

typedef uint32_t socklen_t;

struct linger {
    int l_onoff;
    int l_linger;
};

int socket(int domain, int type, int protocol);
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);
int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
int shutdown(int sockfd, int how);
int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

#endif
