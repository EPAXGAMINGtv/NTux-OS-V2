#ifndef NTUX_TASKMGR_STATS_H
#define NTUX_TASKMGR_STATS_H

#include <syscall.h>
#include <stdint.h>

void taskmgr_sample_stats(ntux_cpu_info_t* cpu_now,
                          ntux_mem_info_t* mem_now,
                          ntux_disk_stats_t* disk_now);

void taskmgr_compute_stats(const ntux_cpu_info_t* cpu_prev,
                           const ntux_cpu_info_t* cpu_now,
                           const ntux_disk_stats_t* disk_prev,
                           const ntux_disk_stats_t* disk_now,
                           uint64_t last_ticks,
                           int* out_cpu_pct,
                           uint64_t* out_rd_bps,
                           uint64_t* out_wr_bps);

#endif
