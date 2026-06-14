#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <args.h>

#define FILE_BUF 4096

static void read_pass(char *buf, int cap) {
    int pos = 0;
    for (;;) {
        long c = sys_getchar();
        if (c < 0) continue;
        if (c == '\n' || c == '\r') break;
        if (c == 127 || c == 8) { if (pos > 0) pos--; }
        else if (c >= 32 && c < 127 && pos < cap - 1) buf[pos++] = (char)c;
    }
    buf[pos] = '\0';
}

void ntux_user_entry(void) {
    int argc = ntux_argc();
    char **argv = ntux_argv();

    const char *username = "root";
    if (argc >= 2) username = argv[1];

    sys_write("su: password: ", 14);
    char pass[64];
    read_pass(pass, sizeof(pass));
    sys_write("\n", 1);

    if (sys_auth_user(username, pass) != 0) {
        sys_write("su: authentication failed\n", 26);
        sys_exit(1);
    }

    sys_write("su: authenticated\n", 18);
    sys_exit(0);
}
