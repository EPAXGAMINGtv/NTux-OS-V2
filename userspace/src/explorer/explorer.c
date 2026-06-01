#include <syscall.h>
#include <window.h>

void ntux_user_entry(void) {
    if (window_init() == 0) {
        (void)window_open_explorer();
        sys_wait_ticks(2);
    }
    sys_exit(0);
}
