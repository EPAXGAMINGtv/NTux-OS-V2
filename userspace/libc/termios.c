#include <termios.h>

#include <errno.h>

int tcgetattr(int fd, struct termios *termios_p) {
    (void)fd;
    if (!termios_p) {
        errno = EINVAL;
        return -1;
    }
    termios_p->c_iflag = 0;
    termios_p->c_oflag = 0;
    termios_p->c_cflag = 0;
    termios_p->c_lflag = ICANON | ECHO | ISIG;
    for (int i = 0; i < NCCS; ++i) termios_p->c_cc[i] = 0;
    return 0;
}

int tcsetattr(int fd, int optional_actions, const struct termios *termios_p) {
    (void)fd;
    (void)optional_actions;
    if (!termios_p) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

void cfmakeraw(struct termios *termios_p) {
    if (!termios_p) return;
    termios_p->c_iflag = 0;
    termios_p->c_oflag = 0;
    termios_p->c_cflag = 0;
    termios_p->c_lflag = 0;
}
