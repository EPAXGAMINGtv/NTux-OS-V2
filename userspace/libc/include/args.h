#ifndef NTUX_ARGS_H
#define NTUX_ARGS_H

int ntux_argc(void);
char **ntux_argv(void);
const char *ntux_arg(int idx);

void ntux_args_init(void);

#endif
