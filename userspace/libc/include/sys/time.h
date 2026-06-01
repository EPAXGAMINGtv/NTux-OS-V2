#ifndef USER_SYS_TIME_H
#define USER_SYS_TIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct timeval {
    int64_t tv_sec;
    int64_t tv_usec;
};

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

int gettimeofday(struct timeval *tv, struct timezone *tz);

#ifdef __cplusplus
}
#endif

#endif
