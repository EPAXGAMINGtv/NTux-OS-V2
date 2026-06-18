#include <syscall.h>
#include <stddef.h>

long ntux_syscall3(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2) {
    uint64_t ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(nr), "D"(a0), "S"(a1), "d"(a2)
        : "memory"
    );
    return (long)ret;
}

long ntux_syscall4(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    uint64_t ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(nr), "D"(a0), "S"(a1), "d"(a2), "c"(a3)
        : "memory"
    );
    return (long)ret;
}

long sys_write(const void *buf, uint64_t len) {
    return ntux_syscall3(INT80_WRITE, (uint64_t)(unsigned long)buf, len, 0);
}

void sys_exit(int code) {
    (void)ntux_syscall3(INT80_EXIT, (uint64_t)(uint32_t)code, 0, 0);
    for (;;) {
        (void)ntux_syscall3(INT80_YIELD, 0, 0, 0);
    }
}

long sys_putchar(char c) {
    return ntux_syscall3(INT80_PUTCHAR, (uint64_t)(uint8_t)c, 0, 0);
}

long sys_getchar(void) {
    return ntux_syscall3(INT80_GETCHAR, 0, 0, 0);
}

long sys_reboot(void) {
    return ntux_syscall3(INT80_REBOOT, 0, 0, 0);
}

long sys_shutdown(void) {
    return ntux_syscall3(INT80_SHUTDOWN, 0, 0, 0);
}

long sys_yield(void) {
    return ntux_syscall3(INT80_YIELD, 0, 0, 0);
}

long sys_console_release(void) {
    return ntux_syscall3(INT80_CONSOLE_RELEASE, 0, 0, 0);
}

long sys_console_is_free(void) {
    return ntux_syscall3(INT80_CONSOLE_IS_FREE, 0, 0, 0);
}

long sys_console_claim(void) {
    return ntux_syscall3(INT80_CONSOLE_CLAIM, 0, 0, 0);
}

long sys_console_force_claim(void) {
    return ntux_syscall3(INT80_CONSOLE_FORCE_CLAIM, 0, 0, 0);
}

long sys_task_add(const char* path) {
    return ntux_syscall3(INT80_TASK_ADD, (uint64_t)(uintptr_t)path, 0, 0);
}

long sys_task_add_module(const char* token) {
    return ntux_syscall3(INT80_TASK_ADD_MODULE, (uint64_t)(uintptr_t)token, 0, 0);
}

long sys_get_tid(void) {
    return ntux_syscall3(INT80_GET_TID, 0, 0, 0);
}

long sys_task_kill(int tid) {
    return ntux_syscall3(INT80_TASK_KILL, (uint64_t)tid, 0, 0);
}

long sys_task_list(ntux_task_info_t* out, uint64_t max_entries, uint64_t* out_count) {
    return ntux_syscall3(INT80_TASK_LIST, (uint64_t)(uintptr_t)out, max_entries, (uint64_t)(uintptr_t)out_count);
}

long sys_module_list(ntux_module_info_t* out, uint64_t max_entries, uint64_t* out_count) {
    return ntux_syscall3(INT80_MODULE_LIST, (uint64_t)(uintptr_t)out, max_entries, (uint64_t)(uintptr_t)out_count);
}

long sys_set_uid(uint32_t uid) {
    return ntux_syscall3(INT80_SET_UID, (uint64_t)uid, 0, 0);
}

uint32_t sys_get_uid(void) {
    return (uint32_t)ntux_syscall3(INT80_GET_UID, 0, 0, 0);
}

long sys_fs_exists(const char* path) {
    return ntux_syscall3(INT80_FS_EXISTS, (uint64_t)(uintptr_t)path, 0, 0);
}

long sys_fs_mkdir(const char* parent, const char* name) {
    return ntux_syscall3(INT80_FS_MKDIR, (uint64_t)(uintptr_t)parent, (uint64_t)(uintptr_t)name, 0);
}

long sys_fs_create_file(const char* parent, const char* name, const void* data, uint64_t len) {
    return ntux_syscall4(INT80_FS_CREATE_FILE, (uint64_t)(uintptr_t)parent, (uint64_t)(uintptr_t)name, (uint64_t)(uintptr_t)data, len);
}

long sys_fs_write_file(const char* path, const void* data, uint64_t len) {
    return ntux_syscall3(INT80_FS_WRITE_FILE, (uint64_t)(uintptr_t)path, (uint64_t)(uintptr_t)data, len);
}

long sys_fs_read_file(const char* path, void* out, uint64_t out_cap, uint64_t* out_len) {
    return ntux_syscall4(INT80_FS_READ_FILE, (uint64_t)(uintptr_t)path, (uint64_t)(uintptr_t)out, out_cap, (uint64_t)(uintptr_t)out_len);
}

long sys_fs_list_dir(const char* path, ntux_dirent_t* out, uint64_t max_entries, uint64_t* out_count) {
    return ntux_syscall4(INT80_FS_LIST_DIR, (uint64_t)(uintptr_t)path, (uint64_t)(uintptr_t)out, max_entries, (uint64_t)(uintptr_t)out_count);
}

long sys_fs_remove(const char* path) {
    return ntux_syscall3(INT80_FS_REMOVE, (uint64_t)(uintptr_t)path, 0, 0);
}

long sys_fs_rename(const char* old_path, const char* new_path) {
    return ntux_syscall3(INT80_FS_RENAME, (uint64_t)(uintptr_t)old_path, (uint64_t)(uintptr_t)new_path, 0);
}

long sys_fs_copy_fast(const char* src, const char* dst) {
    return ntux_syscall3(INT80_FS_COPY_FAST, (uint64_t)(uintptr_t)src, (uint64_t)(uintptr_t)dst, 0);
}

long sys_mouse_get_state(ntux_mouse_state_t* out_state) {
    return ntux_syscall3(INT80_MOUSE_GET_STATE, (uint64_t)(uintptr_t)out_state, 0, 0);
}

long sys_kbd_is_pressed(uint8_t scancode) {
    return ntux_syscall3(INT80_KBD_IS_PRESSED, (uint64_t)scancode, 0, 0);
}

long sys_kbd_get_state(uint8_t* out, uint64_t len) {
    return ntux_syscall3(INT80_KBD_GET_STATE, (uint64_t)(uintptr_t)out, len, 0);
}

long sys_kbd_consume_super_press(void) {
    return ntux_syscall3(INT80_KBD_CONSUME_SUPER_PRESS, 0, 0, 0);
}

long sys_kbd_set_layout(const char* path) {
    return ntux_syscall3(INT80_KBD_SET_LAYOUT, (uint64_t)(uintptr_t)path, 0, 0);
}

long sys_audio_play(const int16_t* samples, uint32_t count) {
    return ntux_syscall3(INT80_AUDIO_PLAY, (uint64_t)(uintptr_t)samples, (uint64_t)count, 0);
}

long sys_audio_stop(void) {
    return ntux_syscall3(INT80_AUDIO_STOP, 0, 0, 0);
}

long sys_audio_status(void) {
    return ntux_syscall3(INT80_AUDIO_STATUS, 0, 0, 0);
}

uint64_t sys_get_ticks(void) {
    return (uint64_t)ntux_syscall3(INT80_GET_TICKS, 0, 0, 0);
}

void sys_wait_ticks(uint64_t ticks) {
    (void)ntux_syscall3(INT80_WAIT_TICKS, ticks, 0, 0);
}

long sys_clear_screen(uint32_t color) {
    return ntux_syscall3(INT80_CLEAR_SCREEN, color, 0, 0);
}

long sys_fb_get_info(ntux_fb_info_t* out_info) {
    return ntux_syscall3(INT80_FB_GET_INFO, (uint64_t)(uintptr_t)out_info, 0, 0);
}

long sys_fb_blit32(const void* rgba, uint32_t width, uint32_t height, uint32_t pitch) {
    return ntux_syscall4(INT80_FB_BLIT32, (uint64_t)(uintptr_t)rgba, width, height, pitch);
}

long sys_set_text_color(uint32_t argb) {
    return ntux_syscall3(INT80_SET_TEXT_COLOR, (uint64_t)argb, 0, 0);
}

long sys_get_time(ntux_time_t* out_time) {
    return ntux_syscall3(INT80_GET_TIME, (uint64_t)(uintptr_t)out_time, 0, 0);
}

long sys_get_timer_hz(void) {
    return ntux_syscall3(INT80_GET_TIMER_HZ, 0, 0, 0);
}

long sys_fs_rescan(void) {
    return ntux_syscall3(INT80_FS_RESCAN, 0, 0, 0);
}

long sys_gpu_get_info(ntux_gpu_info_t* out_info) {
    return ntux_syscall3(INT80_GET_GPU_INFO, (uint64_t)(uintptr_t)out_info, 0, 0);
}

long sys_gpu_get_stats(ntux_gpu_stats_t* out_stats) {
    return ntux_syscall3(INT80_GET_GPU_STATS, (uint64_t)(uintptr_t)out_stats, 0, 0);
}

long sys_block_list(ntux_block_device_info_t* out, uint64_t max_entries, uint64_t* out_count) {
    return ntux_syscall3(INT80_BLK_LIST, (uint64_t)(uintptr_t)out, max_entries, (uint64_t)(uintptr_t)out_count);
}

long sys_block_partitions(uint64_t drive, ntux_partition_info_t* out, uint64_t max_entries, uint64_t* out_count) {
    return ntux_syscall4(INT80_BLK_PART_LIST, drive, (uint64_t)(uintptr_t)out, max_entries, (uint64_t)(uintptr_t)out_count);
}

long sys_block_read(uint64_t drive, uint64_t lba, uint64_t sectors, void* out) {
    return ntux_syscall4(INT80_BLK_READ, drive, lba, sectors, (uint64_t)(uintptr_t)out);
}

long sys_block_write(uint64_t drive, uint64_t lba, uint64_t sectors, const void* in) {
    return ntux_syscall4(INT80_BLK_WRITE, drive, lba, sectors, (uint64_t)(uintptr_t)in);
}

long sys_block_set_mbr_partition(const ntux_mbr_part_req_t* req) {
    return ntux_syscall3(INT80_BLK_SET_MBR_PART, (uint64_t)(uintptr_t)req, 0, 0);
}

long sys_mkfs_ext2(uint64_t drive, uint64_t lba_start, uint64_t sectors) {
    return ntux_syscall3(INT80_MKFS_EXT2, drive, lba_start, sectors);
}

long sys_mkfs_ext4(uint64_t drive, uint64_t lba_start, uint64_t sectors) {
    return ntux_syscall3(INT80_MKFS_EXT4, drive, lba_start, sectors);
}

long sys_mkfs_fat(uint64_t drive, uint64_t lba_start, uint64_t sectors, uint64_t fat_type) {
    return ntux_syscall4(INT80_MKFS_FAT, drive, lba_start, sectors, fat_type);
}

long sys_open(const char* path, uint64_t flags) {
    return ntux_syscall3(INT80_OPEN, (uint64_t)(uintptr_t)path, flags, 0);
}

long sys_read_fd(int fd, void* out, uint64_t len) {
    return ntux_syscall3(INT80_READ, (uint64_t)fd, (uint64_t)(uintptr_t)out, len);
}

long sys_write_fd(int fd, const void* in, uint64_t len) {
    return ntux_syscall3(INT80_WRITE_FD, (uint64_t)fd, (uint64_t)(uintptr_t)in, len);
}

long sys_close(int fd) {
    return ntux_syscall3(INT80_CLOSE, (uint64_t)fd, 0, 0);
}

long sys_ioctl(int fd, uint64_t req, void* arg) {
    return ntux_syscall3(INT80_IOCTL, (uint64_t)fd, req, (uint64_t)(uintptr_t)arg);
}

long sys_lseek(int fd, long offset, int whence) {
    return ntux_syscall3(INT80_LSEEK, (uint64_t)fd, (uint64_t)offset, (uint64_t)whence);
}

long sys_net_ping(const char* host, char* out, uint64_t out_cap) {
    return ntux_syscall3(INT80_NET_PING, (uint64_t)(uintptr_t)host, (uint64_t)(uintptr_t)out, out_cap);
}

long sys_net_http_get(const char* url, char* out, uint64_t out_cap) {
    return ntux_syscall3(INT80_NET_HTTP_GET, (uint64_t)(uintptr_t)url, (uint64_t)(uintptr_t)out, out_cap);
}

long sys_net_debug(char* out, uint64_t out_cap) {
    return ntux_syscall3(INT80_NET_DEBUG, (uint64_t)(uintptr_t)out, out_cap, 0);
}

long sys_net_set_dns(uint32_t ip) {
    return ntux_syscall3(INT80_NET_SET_DNS, (uint64_t)ip, 0, 0);
}

long sys_deskapi_push(const char* buf, uint64_t len) {
    return ntux_syscall3(INT80_DESKAPI_PUSH, (uint64_t)(uintptr_t)buf, len, 0);
}

long sys_deskapi_pop(char* out, uint64_t cap, uint64_t* out_len) {
    return ntux_syscall4(INT80_DESKAPI_POP, (uint64_t)(uintptr_t)out, cap, (uint64_t)(uintptr_t)out_len, 0);
}

long sys_get_mem_info(ntux_mem_info_t* out) {
    return ntux_syscall3(INT80_GET_MEM_INFO, (uint64_t)(uintptr_t)out, 0, 0);
}

long sys_get_disk_stats(ntux_disk_stats_t* out) {
    return ntux_syscall3(INT80_GET_DISK_STATS, (uint64_t)(uintptr_t)out, 0, 0);
}

long sys_get_cpu_info(ntux_cpu_info_t* out) {
    return ntux_syscall3(INT80_GET_CPU_INFO, (uint64_t)(uintptr_t)out, 0, 0);
}

long sys_get_cpu_brand(char* out, uint64_t cap) {
    return ntux_syscall3(INT80_GET_CPU_BRAND, (uint64_t)(uintptr_t)out, cap, 0);
}

long sys_dialog_pop(char* out, uint64_t cap, uint32_t* out_code) {
    return ntux_syscall3(INT80_DIALOG_POP, (uint64_t)(uintptr_t)out, cap, (uint64_t)(uintptr_t)out_code);
}

long sys_dialog_push(int tid, uint32_t code, const char* text) {
    return ntux_syscall3(INT80_DIALOG_PUSH, (uint64_t)tid, (uint64_t)code, (uint64_t)(uintptr_t)text);
}

long sys_geteuid(void) {
    return ntux_syscall3(INT80_GETEUID, 0, 0, 0);
}

long sys_setgid(uint32_t gid) {
    return ntux_syscall3(INT80_SETGID, (uint64_t)gid, 0, 0);
}

long sys_getgid(void) {
    return ntux_syscall3(INT80_GETGID, 0, 0, 0);
}

long sys_setegid(uint32_t egid) {
    return ntux_syscall3(INT80_SETEGID, (uint64_t)egid, 0, 0);
}

long sys_getegid(void) {
    return ntux_syscall3(INT80_GETEGID, 0, 0, 0);
}

long sys_chmod(const char* path, uint32_t mode) {
    return ntux_syscall3(INT80_CHMOD, (uint64_t)(uintptr_t)path, (uint64_t)mode, 0);
}

long sys_chown(const char* path, uint32_t owner, uint32_t group) {
    return ntux_syscall3(INT80_CHOWN, (uint64_t)(uintptr_t)path, (uint64_t)owner, (uint64_t)group);
}

long sys_stat(const char* path, void* buf) {
    return ntux_syscall3(INT80_STAT, (uint64_t)(uintptr_t)path, (uint64_t)(uintptr_t)buf, 0);
}

long sys_umask(uint32_t mask) {
    return ntux_syscall3(INT80_UMASK, (uint64_t)mask, 0, 0);
}

long sys_getgroups(int size, uint32_t* list) {
    return ntux_syscall3(INT80_GETGROUPS, (uint64_t)size, (uint64_t)(uintptr_t)list, 0);
}

long sys_setgroups(int size, const uint32_t* list) {
    return ntux_syscall3(INT80_SETGROUPS, (uint64_t)size, (uint64_t)(uintptr_t)list, 0);
}

long sys_auth_user(const char* name, const char* password) {
    return ntux_syscall3(INT80_AUTH_USER, (uint64_t)(uintptr_t)name, (uint64_t)(uintptr_t)password, 0);
}

long sys_getpwnam(char* out, const char* name) {
    return ntux_syscall3(INT80_GETPWNAM, (uint64_t)(uintptr_t)out, (uint64_t)(uintptr_t)name, 0);
}

long sys_getpwuid(char* out, uint32_t uid) {
    return ntux_syscall3(INT80_GETPWUID, (uint64_t)(uintptr_t)out, (uint64_t)uid, 0);
}

long sys_getgrnam(char* out, const char* name) {
    return ntux_syscall3(INT80_GETGRNAM, (uint64_t)(uintptr_t)out, (uint64_t)(uintptr_t)name, 0);
}

long sys_getgrgid(char* out, uint32_t gid) {
    return ntux_syscall3(INT80_GETGRGID, (uint64_t)(uintptr_t)out, (uint64_t)gid, 0);
}

long sys_getppid(void) {
    return ntux_syscall3(INT80_GETPPID, 0, 0, 0);
}

long sys_setsid(void) {
    return ntux_syscall3(INT80_SETSID, 0, 0, 0);
}

long sys_getpgid(void) {
    return ntux_syscall3(INT80_GETPGID, 0, 0, 0);
}

void *sys_umalloc(size_t size) {
    return (void *)(uintptr_t)ntux_syscall3(INT80_UMALLOC, (uint64_t)size, 0, 0);
}

void sys_ufree(void *ptr) {
    ntux_syscall3(INT80_UFREE, (uint64_t)(uintptr_t)ptr, 0, 0);
}



