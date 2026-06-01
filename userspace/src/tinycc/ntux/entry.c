#include <args.h>
#include <syscall.h>

int main(int argc, char **argv);

void ntux_user_entry(void) {
    ntux_args_init();
    int argc = ntux_argc();
    char **argv = ntux_argv();
    int rc = 0;
    if (!argv || argc <= 0) {
        static char *fallback_argv[] = { (char *)"tcc", 0 };
        rc = main(1, fallback_argv);
    } else {
        rc = main(argc, argv);
    }
    sys_exit(rc);
}
