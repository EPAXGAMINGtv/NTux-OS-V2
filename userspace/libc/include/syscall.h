#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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
    INT80_MODULE_LIST = 116,
    INT80_GET_GPU_INFO = 117,
    INT80_GET_GPU_STATS = 118
};

long ntux_syscall3(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2);
long ntux_syscall4(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3);

typedef struct {
    char name[64];
    uint8_t is_dir;
    uint8_t _pad[7];
    uint64_t size;
} ntux_dirent_t;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t scroll;
    uint8_t left;
    uint8_t right;
    uint8_t middle;
    uint8_t _pad;
} ntux_mouse_state_t;

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
} ntux_fb_info_t;

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} ntux_time_t;

typedef struct {
    char token[64];
    char path[160];
} ntux_module_info_t;

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
} ntux_task_info_t;

typedef struct {
    char name[32];
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t fb_size;
} ntux_gpu_info_t;

typedef struct {
    uint64_t blit_count;
    uint64_t blit_errors;
    uint64_t blit_cycles;
    uint32_t current_memory_usage;
    uint32_t max_memory_usage;
    uint32_t memory_allocations;
    uint64_t ticks;
} ntux_gpu_stats_t;

typedef struct {
    uint64_t total_bytes;
    uint64_t free_bytes;
} ntux_mem_info_t;

typedef struct {
    uint64_t read_bytes;
    uint64_t write_bytes;
} ntux_disk_stats_t;

typedef struct {
    uint64_t ticks;
    uint64_t idle_ticks;
    uint32_t hz;
    uint32_t _pad;
} ntux_cpu_info_t;

typedef struct {
    uint8_t present;
    uint8_t is_atapi;
    uint8_t _pad[6];
    uint64_t sectors;
    char model[41];
} ntux_block_device_info_t;

typedef struct {
    uint8_t index;
    uint8_t type;
    uint8_t bootable;
    uint8_t _pad;
    uint64_t lba_start;
    uint64_t sectors;
} ntux_partition_info_t;

typedef struct {
    uint8_t drive;
    uint8_t part_index;
    uint8_t type;
    uint8_t bootable;
    uint32_t lba_start;
    uint32_t sectors;
} ntux_mbr_part_req_t;

long sys_write(const void *buf, uint64_t len);
void sys_exit(int code);
long sys_putchar(char c);
long sys_getchar(void);
long sys_reboot(void);
long sys_shutdown(void);
long sys_yield(void);
long sys_console_release(void);
long sys_console_is_free(void);
long sys_console_claim(void);
long sys_task_add(const char* path);
long sys_task_add_module(const char* token);
long sys_get_tid(void);
long sys_task_kill(int tid);
long sys_task_list(ntux_task_info_t* out, uint64_t max_entries, uint64_t* out_count);
long sys_module_list(ntux_module_info_t* out, uint64_t max_entries, uint64_t* out_count);
long sys_set_uid(uint32_t uid);
uint32_t sys_get_uid(void);
long sys_fs_exists(const char* path);
long sys_fs_mkdir(const char* parent, const char* name);
long sys_fs_create_file(const char* parent, const char* name, const void* data, uint64_t len);
long sys_fs_write_file(const char* path, const void* data, uint64_t len);
long sys_fs_read_file(const char* path, void* out, uint64_t out_cap, uint64_t* out_len);
long sys_fs_list_dir(const char* path, ntux_dirent_t* out, uint64_t max_entries, uint64_t* out_count);
long sys_fs_remove(const char* path);
long sys_fs_rename(const char* old_path, const char* new_path);
long sys_fs_copy_fast(const char* src, const char* dst);
long sys_mouse_get_state(ntux_mouse_state_t* out_state);
long sys_kbd_is_pressed(uint8_t scancode);
long sys_kbd_get_state(uint8_t* out, uint64_t len);
long sys_kbd_consume_super_press(void);
uint64_t sys_get_ticks(void);
void sys_wait_ticks(uint64_t ticks);
long sys_clear_screen(uint32_t color);
long sys_fb_get_info(ntux_fb_info_t* out_info);
long sys_fb_blit32(const void* rgba, uint32_t width, uint32_t height, uint32_t pitch);
long sys_set_text_color(uint32_t argb);
long sys_get_time(ntux_time_t* out_time);
long sys_get_timer_hz(void);
long sys_fs_rescan(void);
long sys_gpu_get_info(ntux_gpu_info_t* out_info);
long sys_gpu_get_stats(ntux_gpu_stats_t* out_stats);
long sys_block_list(ntux_block_device_info_t* out, uint64_t max_entries, uint64_t* out_count);
long sys_block_partitions(uint64_t drive, ntux_partition_info_t* out, uint64_t max_entries, uint64_t* out_count);
long sys_block_read(uint64_t drive, uint64_t lba, uint64_t sectors, void* out);
long sys_block_write(uint64_t drive, uint64_t lba, uint64_t sectors, const void* in);
long sys_block_set_mbr_partition(const ntux_mbr_part_req_t* req);
long sys_mkfs_ext2(uint64_t drive, uint64_t lba_start, uint64_t sectors);
long sys_mkfs_ext4(uint64_t drive, uint64_t lba_start, uint64_t sectors);
long sys_mkfs_fat(uint64_t drive, uint64_t lba_start, uint64_t sectors, uint64_t fat_type);
long sys_open(const char* path, uint64_t flags);
long sys_read_fd(int fd, void* out, uint64_t len);
long sys_write_fd(int fd, const void* in, uint64_t len);
long sys_close(int fd);
long sys_ioctl(int fd, uint64_t req, void* arg);
long sys_lseek(int fd, long offset, int whence);
long sys_net_ping(const char* host, char* out, uint64_t out_cap);
long sys_net_http_get(const char* url, char* out, uint64_t out_cap);
long sys_net_debug(char* out, uint64_t out_cap);
long sys_net_set_dns(uint32_t ip);
long sys_deskapi_push(const char* buf, uint64_t len);
long sys_deskapi_pop(char* out, uint64_t cap, uint64_t* out_len);
long sys_get_mem_info(ntux_mem_info_t* out);
long sys_get_disk_stats(ntux_disk_stats_t* out);
long sys_get_cpu_info(ntux_cpu_info_t* out);
long sys_get_cpu_brand(char* out, uint64_t cap);
long sys_dialog_pop(char* out, uint64_t cap, uint32_t* out_code);
long sys_dialog_push(int tid, uint32_t code, const char* text);

#ifdef __cplusplus
}
#endif

#endif


