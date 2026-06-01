#include <stdio.h>
#include <syscall.h>

void ntux_user_entry(void) {
    puts("Hello von hellokonsole.elf");
    puts("Diese App laeuft in der NTux-Konsole.");
    int test= 0;
    while (test <= 10)
    {
        test++;
        puts("test");
        /* code */
    }
    
    sys_exit(0);
}
