#ifndef NTUX_DESKTOP_TERMINAL_H
#define NTUX_DESKTOP_TERMINAL_H

#include <stdint.h>
#include "desktop_defs.h"

typedef struct {
    int tid;
    int term_idx;
} desk_term_route_t;

#define DESK_TERM_ROUTE_MAX 64

desk_term_state_t* term_state_for_window(const desk_window_t* w);
desk_term_state_t* term_state_active(void);
void term_print_banner(void);
void term_push_line(const char* s);
void term_push_line_color(const char* s, uint32_t color);
void term_push_line_state(desk_term_state_t* ts, const char* s, uint32_t color);
void term_push_multiline(const char* s);
void term_push_multiline_state(desk_term_state_t* ts, const char* s, uint32_t color);
void term_push_num_u64(uint64_t v, char* out, size_t cap);
void desk_term_write_for_tid(int tid, const char* s);
void desk_term_write(int tid, const char* s);
void term_run_command_line(desk_window_t* tw, const char* line_in);
void term_run_command(void);
void term_route_register(int tid, int term_idx);
int term_route_find(int tid);
void term_cmd_ls(const char* cwd, const char* arg);
void term_cmd_cat(const char* cwd, const char* arg);
void term_cmd_cd(char* cwd, const char* arg);
void term_cmd_mkdir(const char* cwd, const char* arg);
void term_cmd_touch(const char* cwd, const char* arg);
void term_cmd_rm(const char* cwd, const char* arg);
void term_cmd_mv(const char* cwd, const char* old_arg, const char* new_arg);
void term_task_list(void);
void term_cmd_lsblk(void);
void term_cmd_blkrescan(void);
void term_cmd_fdisk(int argc, char* argv[]);
void term_cmd_mkfs(int ext4_mode, int argc, char* argv[]);
void term_cmd_dd(const char* cwd, int argc, char* argv[]);
int term_write_args_file(const char* path, char* argv[], int start, int argc);
void term_write_args_for_tid(int tid, const char* first, char* argv[], int start, int argc);
char poll_char(void);
int poll_special_press(int sc);

extern desk_term_route_t g_term_routes[DESK_TERM_ROUTE_MAX];
extern uint8_t g_term_passthrough;
extern uint8_t g_key_last[128];

#endif
