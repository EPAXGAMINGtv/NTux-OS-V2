#ifndef USER_SYS_IOCTL_H
#define USER_SYS_IOCTL_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct sgttyb {
    int sg_ispeed;
    int sg_ospeed;
    int sg_erase;
    int sg_kill;
    int sg_flags;
};

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

#define TIOCGETP 0x5401
#define TIOCSETP 0x5402
#define TIOCSETN 0x5403
#define TIOCGWINSZ 0x5413

#define CRMOD 0x0002
#define RAW   0x0004

int ioctl(int fd, unsigned long request, void *arg);

#ifdef __cplusplus
}
#endif

#endif
