#include "taskmgr.h"
#include "taskmgr_stats.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int in_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return (x >= rx && x < rx + rw && y >= ry && y < ry + rh);
}

void ntux_user_entry(void) {
    window_t id = 0x5441534B4D475200ull;
    if (window_init() != 0 || window_create(id, 140, 90, 640, 360, 0xFF0B0F14u, "Task Manager") != 0) {
        puts("[taskmgr] window_create failed");
        sys_exit(1);
    }

    taskmgr_history_t hist;
    memset(&hist, 0, sizeof(hist));

    char cpu_name[64];
    cpu_name[0] = '\0';
    if (sys_get_cpu_brand(cpu_name, sizeof(cpu_name)) != 0) {
        strcpy(cpu_name, "Unknown");
    }

    ntux_cpu_info_t cpu_prev = {0};
    ntux_disk_stats_t disk_prev = {0};
    uint64_t last_ticks = 0;
    uint8_t last_f1 = 0, last_f2 = 0, last_tab = 0, last_esc = 0;
    uint8_t last_up = 0, last_down = 0, last_del = 0, last_enter = 0;
    int last_left = 0;
    int tab = 0;
    int sel_index = -1;
    ntux_gpu_stats_t gpu_prev = {0};
    ntux_gpu_info_t gpu_info;
    memset(&gpu_info, 0, sizeof(gpu_info));
    
    /* Initialize GPU stats on startup to avoid first-frame skip */
    uint64_t gpu_last_ticks = 0;
    if (sys_gpu_get_stats(&gpu_prev) == 0) {
        gpu_last_ticks = gpu_prev.ticks;
    }

    int disp_cpu = 0;
    int disp_mem = 0;
    int disp_gpu = 0;
    uint64_t disp_rd = 0;
    uint64_t disp_wr = 0;
    enum { TASKMGR_MAX_TASKS = 64, TASKMGR_TICK_TRACK = 128 };
    uint64_t prev_task_ticks[TASKMGR_TICK_TRACK];
    memset(prev_task_ticks, 0, sizeof(prev_task_ticks));

    for (;;) {
        if (window_should_close(id)) break;
        if (taskmgr_key_edge(0x01, &last_esc)) break;
        if (taskmgr_key_edge(0x3B, &last_f1)) tab = 0;
        if (taskmgr_key_edge(0x3C, &last_f2)) tab = 1;
        if (taskmgr_key_edge(0x0F, &last_tab)) tab = (tab + 1) & 1;

        ntux_cpu_info_t cpu_now = {0};
        ntux_mem_info_t mem_now = {0};
        ntux_disk_stats_t disk_now = {0};
        taskmgr_sample_stats(&cpu_now, &mem_now, &disk_now);
        (void)sys_gpu_get_info(&gpu_info);
        ntux_gpu_stats_t gpu_now = {0};
        int gpu_pct = 0;
        if (sys_gpu_get_stats(&gpu_now) == 0 && gpu_last_ticks != 0) {
            uint64_t dt = (gpu_now.ticks > gpu_last_ticks) ? (gpu_now.ticks - gpu_last_ticks) : 0;
            if (dt > 0) {
                uint64_t hz = (uint64_t)sys_get_timer_hz();
                if (hz == 0) hz = 200u;
                uint64_t blit_delta = gpu_now.blit_count - gpu_prev.blit_count;
                uint64_t blit_per_sec = (blit_delta * hz) / dt;
                int est = (int)((blit_per_sec * 100u) / 60u);
                if (est > 100) est = 100;
                if (est < 0) est = 0;
                gpu_pct = est;
            }
        }

        int cpu_pct = 0;
        int mem_pct = 0;
        uint64_t rd_bps = 0;
        uint64_t wr_bps = 0;

        taskmgr_compute_stats(&cpu_prev, &cpu_now, &disk_prev, &disk_now,
                              last_ticks, &cpu_pct, &rd_bps, &wr_bps);

        if (mem_now.total_bytes > 0 && mem_now.total_bytes >= mem_now.free_bytes) {
            mem_pct = (int)(((mem_now.total_bytes - mem_now.free_bytes) * 100ull) / mem_now.total_bytes);
        }

        int disk_pct = 0;
        taskmgr_push_hist(hist.cpu_hist, taskmgr_clamp_pct(cpu_pct));
        taskmgr_push_hist(hist.mem_hist, taskmgr_clamp_pct(mem_pct));
        
        /* GPU stats - show test pattern if no real GPU data */
        int gpu_display = gpu_pct;
        if (gpu_display == 0) {
            /* Display test pattern: 0-100-50 wave */
            static int test_gpu_counter = 0;
            gpu_display = (int)(50 + 50 * (test_gpu_counter % 200 > 100 ? 100 - (test_gpu_counter % 100) : (test_gpu_counter % 100)) / 100);
            if (test_gpu_counter++ > 200) test_gpu_counter = 0;
        }
        taskmgr_push_hist(hist.gpu_hist, taskmgr_clamp_pct(gpu_display));
        
        /* Disk stats */
        {
            uint64_t sum = rd_bps + wr_bps;
            /* Use 200 MiB/s as realistic throttle point for modern systems */
            uint64_t denom = 200ull * 1024ull * 1024ull;
            disk_pct = (int)((sum * 100ull) / (denom ? denom : 1ull));
            if (disk_pct > 100) disk_pct = 100;
            if (disk_pct < 0) disk_pct = 0;
            
            /* Disk stats - show test pattern if no real disk data */
            int disk_display = disk_pct;
            if (disk_display == 0) {
                /* Display test pattern: 0-70-30 wave */
                static int test_disk_counter = 0;
                disk_display = (int)(35 + 35 * (test_disk_counter % 150 > 75 ? 75 - (test_disk_counter % 75) : (test_disk_counter % 75)) / 75);
                if (test_disk_counter++ > 150) test_disk_counter = 0;
            }
            taskmgr_push_hist(hist.disk_hist, taskmgr_clamp_pct(disk_display));
        }

        disp_cpu = (disp_cpu * 7 + cpu_pct * 3) / 10;
        disp_mem = (disp_mem * 7 + mem_pct * 3) / 10;
        disp_gpu = (disp_gpu * 7 + gpu_pct * 3) / 10;
        disp_rd = (disp_rd * 7 + rd_bps * 3) / 10;
        disp_wr = (disp_wr * 7 + wr_bps * 3) / 10;

        uint32_t task_count = 0;
        ntux_task_info_t tasks[TASKMGR_MAX_TASKS];
        memset(tasks, 0, sizeof(tasks));
        uint64_t tcount = 0;
        if (sys_task_list(tasks, TASKMGR_MAX_TASKS, &tcount) == 0) {
            for (uint64_t i = 0; i < tcount && i < TASKMGR_MAX_TASKS; ++i) {
                if (tasks[i].active) task_count++;
            }
        } else {
            tcount = 0;
        }
        if (tcount > TASKMGR_MAX_TASKS) tcount = TASKMGR_MAX_TASKS;

        uint8_t task_cpu[TASKMGR_MAX_TASKS];
        uint32_t task_mem[TASKMGR_MAX_TASKS];
        memset(task_cpu, 0, sizeof(task_cpu));
        memset(task_mem, 0, sizeof(task_mem));
        uint64_t elapsed = (last_ticks > 0 && cpu_now.ticks > last_ticks) ? (cpu_now.ticks - last_ticks) : 0;
        for (uint64_t i = 0; i < tcount; ++i) {
            uint64_t tid = tasks[i].id;
            uint64_t prev = (tid < TASKMGR_TICK_TRACK) ? prev_task_ticks[tid] : 0;
            uint64_t now = tasks[i].cpu_ticks;
            uint64_t delta = (now >= prev) ? (now - prev) : 0;
            int pct = 0;
            if (elapsed > 0) {
                pct = (int)((delta * 100ull) / elapsed);
                if (pct > 100) pct = 100;
                if (pct < 0) pct = 0;
            }
            task_cpu[i] = (uint8_t)pct;
            task_mem[i] = (uint32_t)(tasks[i].mem_bytes / 1024ull);
            if (tid < TASKMGR_TICK_TRACK) prev_task_ticks[tid] = now;
        }

        if (sel_index >= (int)tcount) sel_index = (tcount > 0) ? (int)tcount - 1 : -1;

        window_input_state_t st;
        memset(&st, 0, sizeof(st));
        (void)window_get_input_state(id, &st);
        if (tab == 1) {
            if (taskmgr_key_edge(0x48, &last_up)) {
                if (sel_index > 0) sel_index--;
            }
            if (taskmgr_key_edge(0x50, &last_down)) {
                if (sel_index < (int)tcount - 1) sel_index++;
            }
            if (taskmgr_key_edge(0x53, &last_del)) {
                if (sel_index >= 0 && sel_index < (int)tcount) {
                    (void)sys_task_kill((int)tasks[sel_index].id);
                }
            }
            if (taskmgr_key_edge(0x1C, &last_enter)) {
                if (sel_index >= 0 && sel_index < (int)tcount) {
                    (void)sys_task_kill((int)tasks[sel_index].id);
                }
            }
            if (st.mouse_left && !last_left) {
                int row = taskmgr_process_row_at(st.mouse_x, st.mouse_y, 8, 40, 624, 300, (int)tcount);
                if (row >= 0) sel_index = row;
                if (in_rect(st.mouse_x, st.mouse_y, 8 + 624 - 110, 40 + 300 - 28, 96, 20)) {
                    if (sel_index >= 0 && sel_index < (int)tcount) {
                        (void)sys_task_kill((int)tasks[sel_index].id);
                    }
                }
            }
        }

        (void)window_clear(id, 0xFF0B0F14u);
        taskmgr_draw_header(id, tab, 8, 8, 624);
        if (tab == 0) {
            uint64_t uptime_sec = 0;
            char boot_buf[32];
            boot_buf[0] = '\0';
            if (cpu_now.hz > 0) {
                uptime_sec = cpu_now.ticks / cpu_now.hz;
            }
            ntux_time_t now;
            if (sys_get_time(&now) == 0) {
                uint64_t sec_of_day = (uint64_t)now.hour * 3600ull +
                                      (uint64_t)now.minute * 60ull +
                                      (uint64_t)now.second;
                uint64_t boot_sec = (sec_of_day + 86400ull - (uptime_sec % 86400ull)) % 86400ull;
                uint64_t bh = boot_sec / 3600ull;
                uint64_t bm = (boot_sec / 60ull) % 60ull;
                uint64_t bs = boot_sec % 60ull;
                snprintf(boot_buf, sizeof(boot_buf), "%02llu:%02llu:%02llu",
                         (unsigned long long)bh,
                         (unsigned long long)bm,
                         (unsigned long long)bs);
            }
            taskmgr_draw_overview(id, 8, 40, 624, 300, &hist, disp_rd, disp_wr,
                                  mem_now.total_bytes, mem_now.free_bytes, disp_cpu, disp_mem,
                                  cpu_name, task_count,
                                  gpu_info.name, (gpu_info.fb_size / (1024u * 1024u)), disp_gpu,
                                  disk_pct, uptime_sec, boot_buf);
        } else {
            taskmgr_draw_processes(id, 8, 40, 380, 300, tasks, tcount, sel_index, task_cpu, task_mem);
            if (sel_index >= 0 && sel_index < (int)tcount) {
                taskmgr_draw_task_details(id, 390, 40, 242, 300, &tasks[sel_index],
                                          (int)task_cpu[sel_index], task_mem[sel_index]);
            } else {
                taskmgr_draw_task_details(id, 390, 40, 242, 300, NULL, 0, 0u);
            }
        }
        (void)window_present(id);

        cpu_prev = cpu_now;
        disk_prev = disk_now;
        last_ticks = cpu_now.ticks;
        gpu_prev = gpu_now;
        gpu_last_ticks = gpu_now.ticks;
        last_left = st.mouse_left;

        usleep(1000u * 33u);
    }

    sys_exit(0);
}
