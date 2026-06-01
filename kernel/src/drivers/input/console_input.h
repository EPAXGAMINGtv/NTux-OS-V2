#ifndef CONSOLE_INPUT_H
#define CONSOLE_INPUT_H

#include <stdint.h>

int console_input_owner_is_tid(int tid);
int console_input_claim_or_is_current(int tid);
void console_input_release_if_tid(int tid);
int console_input_is_free(void);
void console_input_force_owner(int tid);

#endif
