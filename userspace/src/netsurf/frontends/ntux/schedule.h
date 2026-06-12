#ifndef NTUX_SCHEDULE_H
#define NTUX_SCHEDULE_H

#include "utils/errors.h"

nserror monkey_schedule(int t, void (*callback)(void *p), void *p);
int monkey_schedule_run(void);

#endif
