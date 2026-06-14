#ifndef TTY_H
#define TTY_H
#include <stdint.h>
#include <stddef.h>
#define TTY_BUF_SIZE 4096
#define TTY_MAX_TTYS 4

void tty_init(void);
int tty_write(int tty_id, const void* data, size_t len, size_t* written);
int tty_read(int tty_id, void* buf, size_t len, size_t* read);
int tty_ioctl(int tty_id, uint64_t req, void* arg);
int tty_register_all(void);
#endif
