#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include <sys/time.h>
#include <time.h>

static struct tm g_tm;

static void fill_tm_from_kernel(struct tm *out) {
    ntux_time_t now;
    if (sys_get_time(&now) != 0) {
        memset(out, 0, sizeof(*out));
        return;
    }
    out->tm_sec = now.second;
    out->tm_min = now.minute;
    out->tm_hour = now.hour;
    out->tm_mday = now.day;
    out->tm_mon = (int)now.month - 1;
    out->tm_year = (int)now.year - 1900;
    out->tm_wday = 0;
    out->tm_yday = 0;
    out->tm_isdst = 0;
}

time_t time(time_t *tloc) {
    time_t t = (time_t)(sys_get_ticks() / CLOCKS_PER_SEC);
    if (tloc) *tloc = t;
    return t;
}

clock_t clock(void) {
    return (clock_t)sys_get_ticks();
}

double difftime(time_t time1, time_t time0) {
    return (double)(time1 - time0);
}

struct tm *gmtime(const time_t *timep) {
    (void)timep;
    fill_tm_from_kernel(&g_tm);
    return &g_tm;
}

struct tm *localtime(const time_t *timep) {
    (void)timep;
    fill_tm_from_kernel(&g_tm);
    return &g_tm;
}

size_t strftime(char *s, size_t max, const char *format, const struct tm *tm) {
    if (!s || !format || !tm || max == 0) return 0;

    char tmp[64];
    size_t out_len = 0;
    for (const char *p = format; *p && out_len + 1 < max; ++p) {
        if (*p != '%') {
            s[out_len++] = *p;
            continue;
        }
        ++p;
        if (!*p) break;
        switch (*p) {
            case 'Y':
                snprintf(tmp, sizeof(tmp), "%04d", tm->tm_year + 1900);
                break;
            case 'm':
                snprintf(tmp, sizeof(tmp), "%02d", tm->tm_mon + 1);
                break;
            case 'd':
                snprintf(tmp, sizeof(tmp), "%02d", tm->tm_mday);
                break;
            case 'H':
                snprintf(tmp, sizeof(tmp), "%02d", tm->tm_hour);
                break;
            case 'M':
                snprintf(tmp, sizeof(tmp), "%02d", tm->tm_min);
                break;
            case 'S':
                snprintf(tmp, sizeof(tmp), "%02d", tm->tm_sec);
                break;
            case '%':
                tmp[0] = '%';
                tmp[1] = '\0';
                break;
            default:
                tmp[0] = '%';
                tmp[1] = *p;
                tmp[2] = '\0';
                break;
        }
        size_t l = strlen(tmp);
        if (out_len + l >= max) break;
        memcpy(s + out_len, tmp, l);
        out_len += l;
    }
    s[out_len] = '\0';
    return out_len;
}

int gettimeofday(struct timeval *tv, struct timezone *tz) {
    if (tz) {
        tz->tz_minuteswest = 0;
        tz->tz_dsttime = 0;
    }
    if (!tv) return -1;
    uint64_t ticks = sys_get_ticks();
    tv->tv_sec = (int64_t)(ticks / CLOCKS_PER_SEC);
    uint64_t rem = ticks % CLOCKS_PER_SEC;
    tv->tv_usec = (int64_t)((rem * 1000000u) / CLOCKS_PER_SEC);
    return 0;
}
