#ifndef USER_SYS_WAIT_H
#define USER_SYS_WAIT_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int waitstatus;

#define WNOHANG 1

pid_t waitpid(pid_t pid, waitstatus *status, int options);

#ifdef __cplusplus
}
#endif

#endif
