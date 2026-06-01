#include <sys/select.h>

#include <ntux_io.h>
#include <syscall.h>

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
    (void)writefds;
    (void)exceptfds;

    if (nfds <= 0 || !readfds) {
        if (timeout) {
            uint64_t ticks = (uint64_t)((timeout->tv_sec * 100u) + (timeout->tv_usec / 10000u));
            if (ticks > 0) sys_wait_ticks(ticks);
        }
        return 0;
    }

    int ready = 0;
    if (FD_ISSET(0, readfds)) {
        if (ntux_console_peek >= 0) {
            ready = 1;
        } else {
            long ch = sys_getchar();
            if (ch >= 0) {
                ntux_console_peek = (int)ch;
                ready = 1;
            }
        }
    }

    if (!ready && timeout) {
        uint64_t ticks = (uint64_t)((timeout->tv_sec * 100u) + (timeout->tv_usec / 10000u));
        if (ticks > 0) sys_wait_ticks(ticks);
    }
    return ready;
}
