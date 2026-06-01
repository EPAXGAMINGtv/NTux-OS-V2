#include "taskmgr.h"

#include <syscall.h>

void taskmgr_sample_stats(ntux_cpu_info_t* cpu_now,
                          ntux_mem_info_t* mem_now,
                          ntux_disk_stats_t* disk_now) {
    if (cpu_now) (void)sys_get_cpu_info(cpu_now);
    if (mem_now) (void)sys_get_mem_info(mem_now);
    if (disk_now) (void)sys_get_disk_stats(disk_now);
}

void taskmgr_compute_stats(const ntux_cpu_info_t* cpu_prev,
                           const ntux_cpu_info_t* cpu_now,
                           const ntux_disk_stats_t* disk_prev,
                           const ntux_disk_stats_t* disk_now,
                           uint64_t last_ticks,
                           int* out_cpu_pct,
                           uint64_t* out_rd_bps,
                           uint64_t* out_wr_bps) {
    int cpu_pct = 0;
    uint64_t rd_bps = 0;
    uint64_t wr_bps = 0;

    if (cpu_prev && cpu_now && cpu_prev->ticks != 0 && cpu_now->ticks >= cpu_prev->ticks) {
        uint64_t dticks = cpu_now->ticks - cpu_prev->ticks;
        uint64_t didle = (cpu_now->idle_ticks >= cpu_prev->idle_ticks)
            ? (cpu_now->idle_ticks - cpu_prev->idle_ticks) : 0;
        if (dticks > 0) {
            uint64_t busy = (dticks > didle) ? (dticks - didle) : 0;
            cpu_pct = (int)((busy * 100ull) / dticks);
        }
    }

    if (disk_prev && disk_now && last_ticks != 0 && cpu_now && cpu_now->ticks > last_ticks) {
        uint64_t dt = cpu_now->ticks - last_ticks;
        uint64_t hz = cpu_now->hz ? cpu_now->hz : 1000u;
        uint64_t ms = (dt * 1000ull) / hz;
        if (ms == 0) ms = 1;
        uint64_t rd = (disk_now->read_bytes >= disk_prev->read_bytes)
            ? (disk_now->read_bytes - disk_prev->read_bytes) : 0;
        uint64_t wr = (disk_now->write_bytes >= disk_prev->write_bytes)
            ? (disk_now->write_bytes - disk_prev->write_bytes) : 0;
        rd_bps = (rd * 1000ull) / ms;
        wr_bps = (wr * 1000ull) / ms;
    }

    if (out_cpu_pct) *out_cpu_pct = cpu_pct;
    if (out_rd_bps) *out_rd_bps = rd_bps;
    if (out_wr_bps) *out_wr_bps = wr_bps;
}
