#include <syscall.h>
#include <stdio.h>

void ntux_user_entry(void) {
    puts("[terminal] Desktop API removed; terminal.elf is disabled.");
    sys_exit(1);
}
