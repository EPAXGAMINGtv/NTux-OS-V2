#ifndef USER_SYS_SELECT_H
#define USER_SYS_SELECT_H

#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t bits[8];
} fd_set;

#define FD_SETSIZE 256
#define FD_ZERO(p) do { for (int _i = 0; _i < 8; ++_i) (p)->bits[_i] = 0; } while (0)
#define FD_SET(fd, p) do { (p)->bits[(fd) >> 5] |= (uint32_t)(1u << ((fd) & 31)); } while (0)
#define FD_CLR(fd, p) do { (p)->bits[(fd) >> 5] &= (uint32_t)~(1u << ((fd) & 31)); } while (0)
#define FD_ISSET(fd, p) (((p)->bits[(fd) >> 5] >> ((fd) & 31)) & 1u)

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);

#ifdef __cplusplus
}
#endif

#endif
