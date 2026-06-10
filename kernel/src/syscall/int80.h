#ifndef INT80_H
#define INT80_H

#include <stdint.h>

typedef struct {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
} int80_regs_t;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t scroll;
    uint8_t left;
    uint8_t right;
    uint8_t middle;
    uint8_t _pad;
} int80_mouse_state_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint8_t memory_model;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
    uint8_t _pad;
} int80_fb_info_t;

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} int80_time_t;

typedef struct {
    uint64_t id;
    char name[32];
    uint32_t state;
    int32_t running_core;
    int32_t affinity_core;
    uint32_t uid;
    uint32_t active;
    uint64_t cpu_ticks;
    uint64_t mem_bytes;
} int80_task_info_t;

typedef struct {
    uint8_t present;
    uint8_t is_atapi;
    uint8_t _pad[6];
    uint64_t sectors;
    char model[41];
} int80_block_device_info_t;

typedef struct {
    uint8_t index;
    uint8_t type;
    uint8_t bootable;
    uint8_t _pad;
    uint64_t lba_start;
    uint64_t sectors;
} int80_partition_info_t;

typedef struct {
    uint8_t drive;
    uint8_t part_index;
    uint8_t type;
    uint8_t bootable;
    uint32_t lba_start;
    uint32_t sectors;
} int80_mbr_part_req_t;

enum {
    INT80_WRITE = 1,
    INT80_EXIT = 2,
    INT80_PUTCHAR = 3,
    INT80_GET_TICKS = 4,
    INT80_WAIT_TICKS = 5,
    INT80_CLEAR_SCREEN = 6,
    INT80_GETCHAR = 7,
    INT80_REBOOT = 8,
    INT80_SHUTDOWN = 9,
    INT80_YIELD = 10,
    INT80_CONSOLE_RELEASE = 11,
    INT80_CONSOLE_IS_FREE = 12,
    INT80_CONSOLE_CLAIM = 13,
    INT80_CONSOLE_FORCE_CLAIM = 14,
    INT80_TASK_ADD = 20,
    INT80_TASK_LIST = 21,
    INT80_SET_UID = 22,
    INT80_GET_UID = 23,
    INT80_TASK_ADD_MODULE = 24,
    INT80_GET_TID = 25,
    INT80_TASK_KILL = 26,
    INT80_FS_EXISTS = 30,
    INT80_FS_MKDIR = 31,
    INT80_FS_CREATE_FILE = 32,
    INT80_FS_WRITE_FILE = 33,
    INT80_FS_READ_FILE = 34,
    INT80_FS_LIST_DIR = 35,
    INT80_FS_REMOVE = 36,
    INT80_FS_RENAME = 37,
    INT80_FS_RESCAN = 38,
    INT80_FS_COPY_FAST = 39,
    INT80_MOUSE_GET_STATE = 50,
    INT80_KBD_IS_PRESSED = 51,
    INT80_KBD_CONSUME_SUPER_PRESS = 52,
    INT80_KBD_GET_STATE = 53,
    INT80_FB_GET_INFO = 60,
    INT80_FB_BLIT32 = 61,
    INT80_SET_TEXT_COLOR = 62,
    INT80_GET_TIME = 63,
    INT80_GET_TIMER_HZ = 64,
    INT80_BLK_LIST = 70,
    INT80_BLK_PART_LIST = 71,
    INT80_BLK_READ = 72,
    INT80_BLK_WRITE = 73,
    INT80_BLK_SET_MBR_PART = 74,
    INT80_MKFS_EXT2 = 75,
    INT80_MKFS_EXT4 = 76,
    INT80_MKFS_FAT = 77,
    INT80_OPEN = 80,
    INT80_READ = 81,
    INT80_WRITE_FD = 82,
    INT80_CLOSE = 83,
    INT80_IOCTL = 84,
    INT80_LSEEK = 85,
    INT80_NET_PING = 90,
    INT80_NET_HTTP_GET = 91,
    INT80_NET_DEBUG = 92,
    INT80_NET_SET_DNS = 93,
    INT80_DESKAPI_PUSH = 100,
    INT80_DESKAPI_POP = 101,
    INT80_GET_MEM_INFO = 110,
    INT80_GET_DISK_STATS = 111,
    INT80_GET_CPU_INFO = 112,
    INT80_GET_CPU_BRAND = 113,
    INT80_DIALOG_POP = 114,
    INT80_DIALOG_PUSH = 115,
    INT80_MODULE_LIST = 116
};

typedef struct {
    uint64_t total_bytes;
    uint64_t free_bytes;
} int80_mem_info_t;

typedef struct {
    uint64_t read_bytes;
    uint64_t write_bytes;
} int80_disk_stats_t;

typedef struct {
    uint64_t ticks;
    uint64_t idle_ticks;
    uint32_t hz;
    uint32_t _pad;
} int80_cpu_info_t;

uint64_t syscall_int80_dispatch(int80_regs_t *regs);
void syscall_user_thread_exit(int80_regs_t *regs);

#endif


