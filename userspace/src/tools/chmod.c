#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <args.h>

static int parse_mode(const char *s) {
    int mode = 0;
    if (!s) return -1;
    while (*s >= '0' && *s <= '7') {
        mode = (mode << 3) | (*s++ - '0');
    }
    if (*s != '\0') return -1;
    return mode;
}

void ntux_user_entry(void) {
    int argc = ntux_argc();
    char **argv = ntux_argv();

    if (argc < 3) {
        sys_write("chmod: usage: chmod mode path\n", 30);
        sys_exit(1);
    }

    int mode = parse_mode(argv[1]);
    if (mode < 0) {
        sys_write("chmod: invalid mode\n", 20);
        sys_exit(1);
    }

    const char *path = argv[2];
    long r = sys_chmod(path, (uint32_t)mode);
    if (r != 0) {
        sys_write("chmod: failed\n", 15);
        sys_exit(1);
    }
    sys_exit(0);
}
