#include <syscall.h>

#include "desktop.h"

void ntux_user_entry(void) {
    if (desktop_init() != 0) {
        sys_exit(1);
    }
    desktop_run();
}
