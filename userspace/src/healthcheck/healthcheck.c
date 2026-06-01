#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <syscall.h>
#include <args.h>

typedef struct {
    const char* name;
    int (*fn)(char* msg, size_t cap);
} hc_test_t;

static void write_line(const char* s) {
    if (s) puts(s);
}

static int test_ticks(char* msg, size_t cap) {
    uint64_t t0 = sys_get_ticks();
    sys_wait_ticks(2);
    uint64_t t1 = sys_get_ticks();
    if (t1 <= t0) {
        snprintf(msg, cap, "ticks not advancing (t0=%llu t1=%llu)",
                 (unsigned long long)t0, (unsigned long long)t1);
        return -1;
    }
    snprintf(msg, cap, "ticks ok (delta=%llu)", (unsigned long long)(t1 - t0));
    return 0;
}

static int test_timer_hz(char* msg, size_t cap) {
    long hz = sys_get_timer_hz();
    if (hz <= 0) {
        snprintf(msg, cap, "timer hz invalid (%ld)", hz);
        return -1;
    }
    snprintf(msg, cap, "timer hz=%ld", hz);
    return 0;
}

static int test_time(char* msg, size_t cap) {
    ntux_time_t t;
    if (sys_get_time(&t) != 0) {
        snprintf(msg, cap, "sys_get_time failed");
        return -1;
    }
    if (t.hour >= 24 || t.minute >= 60 || t.second >= 60 || t.month == 0 || t.month > 12 || t.day == 0 || t.day > 31) {
        snprintf(msg, cap, "rtc out of range: %u-%u-%u %u:%u:%u",
                 t.year, t.month, t.day, t.hour, t.minute, t.second);
        return -1;
    }
    snprintf(msg, cap, "rtc %04u-%02u-%02u %02u:%02u:%02u",
             t.year, t.month, t.day, t.hour, t.minute, t.second);
    return 0;
}

static int test_fs(char* msg, size_t cap) {
    const char* dir = "/tmp";
    const char* name = "healthcheck.tmp";
    const char* path = "/tmp/healthcheck.tmp";
    const char* payload = "ntux-healthcheck";
    uint64_t out_len = 0;
    char buf[64];
    if (sys_fs_exists(dir) <= 0) {
        snprintf(msg, cap, "skip: /tmp missing");
        return 1;
    }
    if (sys_fs_write_file(path, payload, (uint64_t)strlen(payload)) != 0) {
        if (sys_fs_create_file(dir, name, payload, (uint64_t)strlen(payload)) != 0) {
            snprintf(msg, cap, "write/create failed");
            return -1;
        }
    }
    if (sys_fs_read_file(path, buf, sizeof(buf) - 1u, &out_len) != 0 || out_len == 0) {
        snprintf(msg, cap, "read failed");
        (void)sys_fs_remove(path);
        return -1;
    }
    if (out_len >= sizeof(buf)) out_len = sizeof(buf) - 1u;
    buf[out_len] = '\0';
    (void)sys_fs_remove(path);
    if (strncmp(buf, payload, strlen(payload)) != 0) {
        snprintf(msg, cap, "mismatch '%s'", buf);
        return -1;
    }
    snprintf(msg, cap, "fs ok (rw /tmp)");
    return 0;
}

static int test_mem(char* msg, size_t cap) {
    ntux_mem_info_t mi;
    if (sys_get_mem_info(&mi) != 0 || mi.total_bytes == 0) {
        snprintf(msg, cap, "mem info failed");
        return -1;
    }
    snprintf(msg, cap, "mem total=%llu free=%llu",
             (unsigned long long)mi.total_bytes,
             (unsigned long long)mi.free_bytes);
    return 0;
}

static int test_task_list(char* msg, size_t cap) {
    ntux_task_info_t tasks[8];
    uint64_t count = 0;
    if (sys_task_list(tasks, 8, &count) != 0 || count == 0) {
        snprintf(msg, cap, "task list failed");
        return -1;
    }
    snprintf(msg, cap, "tasks=%llu", (unsigned long long)count);
    return 0;
}

static int test_args(char* msg, size_t cap) {
    int argc = ntux_argc();
    char **argv = ntux_argv();
    if (argc <= 0 || !argv) {
        snprintf(msg, cap, "argc/argv unavailable");
        return -1;
    }
    snprintf(msg, cap, "argc=%d argv0=%s", argc, argv[0] ? argv[0] : "(null)");
    return 0;
}

static const hc_test_t g_tests[] = {
    {"ticks", test_ticks},
    {"timer_hz", test_timer_hz},
    {"rtc_time", test_time},
    {"fs_tmp", test_fs},
    {"mem_info", test_mem},
    {"task_list", test_task_list},
    {"args", test_args}
};

void ntux_user_entry(void) {
    write_line("NTux Healthcheck");
    write_line("----------------");
    int pass = 0;
    int fail = 0;
    int skip = 0;
    char msg[128];

    for (int i = 0; i < (int)(sizeof(g_tests) / sizeof(g_tests[0])); ++i) {
        msg[0] = '\0';
        int rc = g_tests[i].fn(msg, sizeof(msg));
        if (rc == 0) {
            printf("[ok]   %s: %s\n", g_tests[i].name, msg);
            pass++;
        } else if (rc > 0) {
            printf("[skip] %s: %s\n", g_tests[i].name, msg);
            skip++;
        } else {
            printf("[fail] %s: %s\n", g_tests[i].name, msg);
            fail++;
        }
    }

    printf("\nResult: %d ok, %d fail, %d skip\n", pass, fail, skip);
    sys_exit(fail == 0 ? 0 : 1);
}
