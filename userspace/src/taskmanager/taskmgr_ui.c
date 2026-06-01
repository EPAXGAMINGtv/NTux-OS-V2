#include "taskmgr.h"

#include <stdio.h>
#include <string.h>
#include <syscall.h>

static void draw_panel(window_t id, int x, int y, int w, int h, uint32_t fill) {
    (void)window_draw_rect(id, x, y, w, h, fill, 1);
}

static void draw_hist_smooth(window_t id, int x, int y, int w, int h, const uint8_t* hist,
                             uint32_t color) {
    if (w <= 0 || h <= 0) return;
    int step = (TASKMGR_HIST_LEN > 1) ? (w / (TASKMGR_HIST_LEN - 1)) : w;
    if (step < 1) step = 1;
    int px = x;
    int py = y + h - (int)((hist[0] * (uint32_t)h) / 100u);
    for (int i = 1; i < TASKMGR_HIST_LEN; ++i) {
        int nx = x + i * step;
        int ny = y + h - (int)((hist[i] * (uint32_t)h) / 100u);
        (void)window_draw_line(id, px, py, nx, ny, color);
        px = nx;
        py = ny;
    }
}

static void sanitize_name(const char* in, char* out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!in) return;
    size_t w = 0;
    for (size_t i = 0; in[i] && w + 1 < cap; ++i) {
        unsigned char c = (unsigned char)in[i];
        if (c < 32 || c > 126) c = '?';
        out[w++] = (char)c;
    }
    out[w] = '\0';
}

static int name_is_valid(const char* s) {
    if (!s || !s[0]) return 0;
    int letters = 0;
    int bad = 0;
    for (int i = 0; s[i] && i < 31; ++i) {
        char c = s[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) letters++;
        if (c == '?' || c == '%' || c == '@') bad++;
    }
    if (letters < 1 && bad > 2) return 0;
    return 1;
}

static const char* task_state_name(uint32_t state) {
    switch (state) {
        case 0: return "Ready";
        case 1: return "Running";
        case 2: return "Waiting";
        case 3: return "Stopped";
        case 4: return "Zombie";
        case 5: return "Blocked";
        default: return "Unknown";
    }
}

void taskmgr_draw_header(window_t id, int tab, int x, int y, int w) {
    const uint32_t bg = 0xFF141C24u;
    const uint32_t sel = 0xFF2E455Cu;
    const uint32_t text = 0xFFE5F1FFu;
    const uint32_t dim = 0xFF9FB3C6u;
    (void)window_draw_rect(id, x, y, w, 24, bg, 1);
    (void)window_draw_rect(id, x + 6, y + 3, 120, 18, tab == 0 ? sel : 0x00000000u, tab == 0);
    (void)window_draw_rect(id, x + 130, y + 3, 120, 18, tab == 1 ? sel : 0x00000000u, tab == 1);
    (void)window_draw_text(id, x + 14, y + 7, tab == 0 ? text : dim, "Overview");
    (void)window_draw_text(id, x + 138, y + 7, tab == 1 ? text : dim, "Processes");
    (void)window_draw_text(id, x + w - 180, y + 7, dim, "F1 Overview  F2 Processes");
}

void taskmgr_draw_overview(window_t id, int x, int y, int w, int h,
                           const taskmgr_history_t* hist,
                           uint64_t rd_bps, uint64_t wr_bps,
                           uint64_t mem_total, uint64_t mem_free,
                           int cpu_pct, int mem_pct,
                           const char* cpu_name, uint32_t task_count,
                           const char* gpu_name, uint32_t gpu_mem_mb, int gpu_pct,
                           int disk_pct, uint64_t uptime_sec, const char* boot_time) {
    char line[128];
    const uint32_t panel = 0xFF0B1016u;
    const uint32_t text = 0xFFE5F1FFu;
    const uint32_t dim = 0xFF9FB3C6u;
    (void)window_draw_rect(id, x, y, w, h, panel, 1);

    snprintf(line, sizeof(line), "%s", (cpu_name && cpu_name[0]) ? cpu_name : "Unknown CPU");
    (void)window_draw_text(id, x + 10, y + 10, text, line);
    snprintf(line, sizeof(line), "Tasks running: %u", task_count);
    (void)window_draw_text(id, x + 10, y + 24, dim, line);
    snprintf(line, sizeof(line), "GPU %d%%  Disk %d%%", gpu_pct, disk_pct);
    (void)window_draw_text(id, x + 220, y + 10, dim, line);
    {
        uint64_t hrs = uptime_sec / 3600ull;
        uint64_t mins = (uptime_sec / 60ull) % 60ull;
        uint64_t secs = uptime_sec % 60ull;
        snprintf(line, sizeof(line), "Uptime: %02llu:%02llu:%02llu",
                 (unsigned long long)hrs,
                 (unsigned long long)mins,
                 (unsigned long long)secs);
        (void)window_draw_text(id, x + 220, y + 24, dim, line);
    }
    if (boot_time && boot_time[0]) {
        snprintf(line, sizeof(line), "Boot: %s", boot_time);
        (void)window_draw_text(id, x + 420, y + 24, dim, line);
    }

    int card_w = (w - 24) / 2;
    int card_h = 110;
    int col0 = x + 6;
    int col1 = x + 12 + card_w;
    int row0 = y + 44;
    int row1 = row0 + card_h + 18;

    draw_panel(id, col0, row0, card_w, card_h, 0xFF111B24u);
    (void)window_draw_text(id, col0 + 10, row0 + 10, 0xFFD7E6F7u, "CPU");
    snprintf(line, sizeof(line), "%d%%", cpu_pct);
    (void)window_draw_text(id, col0 + 10, row0 + 28, 0xFFEAF4FFu, line);
    draw_hist_smooth(id, col0 + 10, row0 + 52, card_w - 20, 46, hist->cpu_hist, 0xFF4EC9FFu);

    draw_panel(id, col1, row0, card_w, card_h, 0xFF111B24u);
    (void)window_draw_text(id, col1 + 10, row0 + 10, 0xFFD7E6F7u, "Memory");
    snprintf(line, sizeof(line), "%d%%", mem_pct);
    (void)window_draw_text(id, col1 + 10, row0 + 28, 0xFFEAF4FFu, line);
    if (mem_total > 0) {
        snprintf(line, sizeof(line), "%llu / %llu MiB",
            (unsigned long long)((mem_total - mem_free) / (1024ull * 1024ull)),
            (unsigned long long)(mem_total / (1024ull * 1024ull)));
        (void)window_draw_text(id, col1 + 10, row0 + 46, dim, line);
    }
    draw_hist_smooth(id, col1 + 10, row0 + 66, card_w - 20, 36, hist->mem_hist, 0xFF9ED1FFu);

    draw_panel(id, col0, row1, card_w, card_h, 0xFF111B24u);
    (void)window_draw_text(id, col0 + 10, row1 + 10, 0xFFD7E6F7u, "GPU");
    snprintf(line, sizeof(line), "%d%%", gpu_pct);
    (void)window_draw_text(id, col0 + 10, row1 + 28, 0xFFEAF4FFu, line);
    snprintf(line, sizeof(line), "%s  %u MiB",
        (gpu_name && gpu_name[0]) ? gpu_name : "Unknown GPU", gpu_mem_mb);
    (void)window_draw_text(id, col0 + 10, row1 + 46, dim, line);
    draw_hist_smooth(id, col0 + 10, row1 + 66, card_w - 20, 36, hist->gpu_hist, 0xFFFFC67Du);

    draw_panel(id, col1, row1, card_w, card_h, 0xFF111B24u);
    (void)window_draw_text(id, col1 + 10, row1 + 10, 0xFFD7E6F7u, "Disk");
    snprintf(line, sizeof(line), "%d%%", disk_pct);
    (void)window_draw_text(id, col1 + 10, row1 + 26, 0xFFEAF4FFu, line);
    snprintf(line, sizeof(line), "R: %llu KiB/s", (unsigned long long)(rd_bps / 1024ull));
    (void)window_draw_text(id, col1 + 64, row1 + 26, 0xFFEAF4FFu, line);
    snprintf(line, sizeof(line), "W: %llu KiB/s", (unsigned long long)(wr_bps / 1024ull));
    (void)window_draw_text(id, col1 + 64, row1 + 40, 0xFFEAF4FFu, line);
    draw_hist_smooth(id, col1 + 10, row1 + 66, card_w - 20, 36, hist->disk_hist, 0xFF7FDBA7u);
}

void taskmgr_draw_processes(window_t id, int x, int y, int w, int h,
                            const ntux_task_info_t* tasks, uint64_t count, int sel_index,
                            const uint8_t* cpu_pct, const uint32_t* mem_kib) {
    const uint32_t panel = 0xFF0F161Eu;
    const uint32_t header = 0xFF1B2633u;
    const uint32_t row_a = 0xFF121D26u;
    const uint32_t row_b = 0xFF101922u;
    const uint32_t text = 0xFFE5F1FFu;
    const uint32_t dim = 0xFF9FB3C6u;
    (void)window_draw_rect(id, x, y, w, h, panel, 1);
    (void)window_draw_rect(id, x + 6, y + 8, w - 12, 22, header, 1);
    (void)window_draw_text(id, x + 12, y + 12, text, "ID   NAME            UID STATE CORE  CPU  MEM");
    (void)window_draw_text(id, x + 12, y + h - 22, dim, "↑↓:select  Del/↵:kill");
    
    int row = 0;
    int row_y = y + 34;
    int max_rows = ((h - 56) / 14);
    if (max_rows < 1) max_rows = 1;
    
    for (uint64_t i = 0; i < count && row < max_rows; ++i) {
        char line[96];
        char name_buf[20];
        char clean[20];
        
        const char* nm = tasks[i].name[0] ? tasks[i].name : 0;
        if (!nm) {
            snprintf(name_buf, sizeof(name_buf), "tid%llu", (unsigned long long)tasks[i].id);
            nm = name_buf;
        } else {
            size_t len = strlen(nm);
            /* Strip .elf extension */
            if (len > 4 && nm[len - 4] == '.' && nm[len - 3] == 'e' && nm[len - 2] == 'l' && nm[len - 1] == 'f') {
                size_t keep = len - 4;
                if (keep >= sizeof(name_buf)) keep = sizeof(name_buf) - 1;
                memcpy(name_buf, nm, keep);
                name_buf[keep] = '\0';
                nm = name_buf;
            }
            /* Sanitize and validate name */
            sanitize_name(nm, clean, sizeof(clean));
            if (name_is_valid(clean)) {
                nm = clean;
            } else {
                /* Fall back to tid if name is invalid */
                snprintf(name_buf, sizeof(name_buf), "tid%llu", (unsigned long long)tasks[i].id);
                nm = name_buf;
            }
        }
        
        int cpu = (cpu_pct && i < count) ? (int)cpu_pct[i] : 0;
        uint32_t mem = (mem_kib && i < count) ? mem_kib[i] : 0u;
        snprintf(line, sizeof(line), "%02llu  %-14s  %02u  %s %d  %3d%% %4uK",
            (unsigned long long)tasks[i].id,
            nm,
            tasks[i].uid,
            task_state_name(tasks[i].state),
            tasks[i].running_core,
            cpu,
            (unsigned int)mem);
        uint32_t bg = (row & 1) ? row_a : row_b;
        if (row == sel_index) bg = 0xFF2A3950u;
        (void)window_draw_rect(id, x + 8, row_y + row * 14, w - 16, 13, bg, 1);
        (void)window_draw_text(id, x + 12, row_y + row * 14, text, line);
        row++;
    }
}

void taskmgr_draw_task_details(window_t id, int x, int y, int w, int h,
                               const ntux_task_info_t* task, int cpu_pct, uint32_t mem_kib) {
    char line[128];
    const uint32_t panel = 0xFF0B1016u;
    const uint32_t header = 0xFF1B2633u;
    const uint32_t text = 0xFFE5F1FFu;
    const uint32_t dim = 0xFF9FB3C6u;
    
    (void)window_draw_rect(id, x, y, w, h, panel, 1);
    (void)window_draw_rect(id, x + 6, y + 8, w - 12, 22, header, 1);
    (void)window_draw_text(id, x + 12, y + 12, text, "Task Details");
    
    int line_y = y + 40;
    int line_h = 18;
    
    if (task) {
        char name_buf[32];
        const char* name = task->name[0] ? task->name : "Unknown";
        size_t len = strlen(name);
        if (len > 4 && name[len-4] == '.' && name[len-3] == 'e' && name[len-2] == 'l' && name[len-1] == 'f') {
            size_t keep = len - 4;
            if (keep >= sizeof(name_buf)) keep = sizeof(name_buf) - 1;
            memcpy(name_buf, name, keep);
            name_buf[keep] = '\0';
            name = name_buf;
        }
        
        snprintf(line, sizeof(line), "Task ID:  %llu", (unsigned long long)task->id);
        (void)window_draw_text(id, x + 12, line_y, text, line);
        line_y += line_h;
        
        snprintf(line, sizeof(line), "Name:     %s", name);
        (void)window_draw_text(id, x + 12, line_y, text, line);
        line_y += line_h;
        
        snprintf(line, sizeof(line), "UID:      %u", task->uid);
        (void)window_draw_text(id, x + 12, line_y, text, line);
        line_y += line_h;
        
        snprintf(line, sizeof(line), "State:    %s", task_state_name(task->state));
        (void)window_draw_text(id, x + 12, line_y, text, line);
        line_y += line_h;

        snprintf(line, sizeof(line), "CPU:      %d%%", cpu_pct);
        (void)window_draw_text(id, x + 12, line_y, text, line);
        line_y += line_h;

        snprintf(line, sizeof(line), "Memory:   %u KiB", (unsigned int)mem_kib);
        (void)window_draw_text(id, x + 12, line_y, text, line);
        line_y += line_h;
        
        snprintf(line, sizeof(line), "Running on Core: %d", task->running_core);
        (void)window_draw_text(id, x + 12, line_y, text, line);
        line_y += line_h;
        
        snprintf(line, sizeof(line), "Affinity Core:   %d", task->affinity_core);
        (void)window_draw_text(id, x + 12, line_y, text, line);
        line_y += line_h;
        
        snprintf(line, sizeof(line), "Active:   %s", task->active ? "Yes" : "No");
        (void)window_draw_text(id, x + 12, line_y, text, line);
    } else {
        (void)window_draw_text(id, x + 12, line_y, dim, "No task selected");
    }
}

int taskmgr_process_row_at(int mx, int my, int x, int y, int w, int h, int row_count) {
    int row_y = y + 34;
    int row_h = 14;
    int max_rows = row_count;
    if (max_rows > 14) max_rows = 14;
    int list_h = max_rows * row_h;
    if (mx < x + 8 || mx >= x + w - 8) return -1;
    if (my < row_y || my >= row_y + list_h) return -1;
    int rel = my - row_y;
    int idx = rel / row_h;
    if (idx < 0 || idx >= max_rows) return -1;
    return idx;
}

int taskmgr_key_edge(uint8_t sc, uint8_t* last) {
    int now = (sys_kbd_is_pressed(sc) > 0) ? 1 : 0;
    int pressed = (now && !*last) ? 1 : 0;
    *last = (uint8_t)now;
    return pressed;
}

void taskmgr_push_hist(uint8_t* hist, uint8_t v) {
    memmove(hist, hist + 1, TASKMGR_HIST_LEN - 1);
    hist[TASKMGR_HIST_LEN - 1] = v;
}

uint8_t taskmgr_clamp_pct(int v) {
    if (v < 0) return 0;
    if (v > 100) return 100;
    return (uint8_t)v;
}
