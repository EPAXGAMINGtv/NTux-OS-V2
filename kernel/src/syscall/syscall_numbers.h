#pragma once

#define SYSCALL_PRINT            0
#define SYSCALL_PUTCHAR          1
#define SYSCALL_GET_TICKS        2
#define SYSCALL_WAIT_TICKS       3
#define SYSCALL_CLEAR_SCREEN     4

#define SYSCALL_FS_EXISTS        20
#define SYSCALL_FS_MKDIR         21
#define SYSCALL_FS_CREATE_FILE   22
#define SYSCALL_FS_WRITE_FILE    23
#define SYSCALL_FS_READ_FILE     24
#define SYSCALL_FS_REMOVE        25
#define SYSCALL_FS_RENAME        26
#define SYSCALL_FS_COPY_FAST     27
