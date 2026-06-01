#ifndef NTUX_TASKMGR_H
#define NTUX_TASKMGR_H

#include <stdint.h>
#include <syscall.h>
#include <window.h>

#define TASKMGR_HIST_LEN 64

typedef struct {
    uint8_t cpu_hist[TASKMGR_HIST_LEN];
    uint8_t mem_hist[TASKMGR_HIST_LEN];
    uint8_t gpu_hist[TASKMGR_HIST_LEN];
    uint8_t disk_hist[TASKMGR_HIST_LEN];
} taskmgr_history_t;

void taskmgr_draw_header(window_t id, int tab, int x, int y, int w);
void taskmgr_draw_overview(window_t id, int x, int y, int w, int h,
                           const taskmgr_history_t* hist,
                           uint64_t rd_bps, uint64_t wr_bps,
                           uint64_t mem_total, uint64_t mem_free,
                           int cpu_pct, int mem_pct,
                           const char* cpu_name, uint32_t task_count,
                           const char* gpu_name, uint32_t gpu_mem_mb, int gpu_pct,
                           int disk_pct, uint64_t uptime_sec, const char* boot_time);
void taskmgr_draw_processes(window_t id, int x, int y, int w, int h,
                            const ntux_task_info_t* tasks, uint64_t count, int sel_index,
                            const uint8_t* cpu_pct, const uint32_t* mem_kib);
void taskmgr_draw_task_details(window_t id, int x, int y, int w, int h,
                               const ntux_task_info_t* task, int cpu_pct, uint32_t mem_kib);
int taskmgr_process_row_at(int mx, int my, int x, int y, int w, int h, int row_count);

int taskmgr_key_edge(uint8_t sc, uint8_t* last);
void taskmgr_push_hist(uint8_t* hist, uint8_t v);
uint8_t taskmgr_clamp_pct(int v);

#endif
