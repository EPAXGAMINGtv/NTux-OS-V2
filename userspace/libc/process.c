#include <signal.h>
#include <unistd.h>

#include <errno.h>
#include <string.h>

int kill(pid_t pid, int sig) {
    (void)pid;
    (void)sig;
    errno = ESRCH;
    return -1;
}

int gethostname(char *name, size_t len) {
    if (!name || len == 0) {
        errno = EINVAL;
        return -1;
    }
    const char *host = "ntux";
    size_t n = strlen(host);
    if (n + 1 > len) n = len - 1;
    memcpy(name, host, n);
    name[n] = '\0';
    return 0;
}
