#include <sys/wait.h>

#include <errno.h>

pid_t waitpid(pid_t pid, waitstatus *status, int options) {
    (void)pid;
    (void)status;
    (void)options;
    errno = ECHILD;
    return -1;
}
