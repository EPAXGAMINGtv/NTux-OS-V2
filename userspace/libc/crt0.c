#include <stdint.h>
#include <syscall.h>
#include <args.h>

typedef void (*init_fini_fn_t)(void);

extern init_fini_fn_t __init_array_start[];
extern init_fini_fn_t __init_array_end[];
extern init_fini_fn_t __fini_array_start[];
extern init_fini_fn_t __fini_array_end[];

extern void ntux_user_entry(void);

static void run_init_array(void) {
    for (init_fini_fn_t* fn = __init_array_start; fn < __init_array_end; ++fn) {
        if (*fn) (*fn)();
    }
}

static void run_fini_array(void) {
    for (init_fini_fn_t* fn = __fini_array_start; fn < __fini_array_end; ++fn) {
        if (*fn) (*fn)();
    }
}

void _start(void) {
    run_init_array();
    ntux_args_init();
    ntux_user_entry();
    run_fini_array();
    sys_exit(0);
}
