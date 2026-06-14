#ifndef NTUX_DESKTOP_H
#define NTUX_DESKTOP_H

#include <syscall.h>

int desktop_init(void);
void desktop_run(void);
void desktop_reload_config(void);
void desktop_get_local_time(ntux_time_t* t);

#endif
