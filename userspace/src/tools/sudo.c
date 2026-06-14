#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <args.h>

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

    if (sys_get_uid() == 0) {
        sys_write("sudo: already root\n", 19);
        sys_exit(0);
    }

    sys_write("sudo: password for root: ", 25);
    char pass[64];
    read_pass(pass, sizeof(pass));
    sys_write("\n", 1);

    if (sys_auth_user("root", pass) != 0) {
        sys_write("sudo: authentication failed\n", 28);
        sys_exit(1);
    }

    sys_set_uid(0);
    sys_write("sudo: authenticated as root\n", 28);
    sys_exit(0);
}
