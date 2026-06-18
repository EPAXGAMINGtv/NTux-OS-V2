#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <window.h>

#define LINE_MAX 256
#define ARGV_MAX 8
#define PATH_MAX 256
#define LS_MAX 64
#define CAT_MAX 4096

static void print_u64(uint64_t v) {
    char buf[32];
    int p = 0;
    if (v == 0) { putchar('0'); return; }
    while (v > 0 && p < (int)sizeof(buf)) {
        buf[p++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (p > 0) putchar(buf[--p]);
}

static void print_two_digits(uint32_t v) {
    putchar((int)('0' + ((v / 10u) % 10u)));
    putchar((int)('0' + (v % 10u)));
}

static int split_args(char* line, char* argv[ARGV_MAX]) {
    int argc = 0;
    char* p = line;
    while (*p && argc < ARGV_MAX) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (!*p) break;
        *p++ = '\0';
    }
    return argc;
}

static int normalize_path(const char* cwd, const char* in, char out[PATH_MAX]) {
    char temp[PATH_MAX];
    size_t tlen = 0;
    if (!in || !in[0]) return -1;
    if (in[0] == '/') {
        while (in[tlen] && tlen + 1 < sizeof(temp)) {
            temp[tlen] = in[tlen];
            tlen++;
        }
        temp[tlen] = '\0';
    } else {
        size_t clen = strlen(cwd);
        if (clen == 0 || cwd[0] != '/') return -1;
        if (clen >= sizeof(temp) - 1) return -1;
        memcpy(temp, cwd, clen);
        tlen = clen;
        if (tlen > 1 && temp[tlen - 1] == '/') tlen--;
        if (tlen + 1 >= sizeof(temp)) return -1;
        temp[tlen++] = '/';
        while (*in && tlen + 1 < sizeof(temp)) temp[tlen++] = *in++;
        temp[tlen] = '\0';
    }
    const char* src = temp;
    out[0] = '/'; out[1] = '\0';
    while (*src) {
        while (*src == '/') src++;
        if (!*src) break;
        char seg[PATH_MAX];
        size_t sl = 0;
        while (src[sl] && src[sl] != '/' && sl + 1 < sizeof(seg)) {
            seg[sl] = src[sl]; sl++;
        }
        seg[sl] = '\0';
        src += sl;
        if (strcmp(seg, ".") == 0) continue;
        if (strcmp(seg, "..") == 0) {
            size_t ol = strlen(out);
            if (ol > 1) {
                size_t j = ol - 1;
                while (j > 0 && out[j] != '/') j--;
                if (j == 0) out[1] = '\0';
                else out[j] = '\0';
            }
            continue;
        }
        size_t ol = strlen(out);
        if (ol > 1) { if (ol + 1 >= PATH_MAX) return -1; out[ol++] = '/'; out[ol] = '\0'; }
        if (ol + sl >= PATH_MAX) return -1;
        memcpy(out + ol, seg, sl + 1);
    }
    return 0;
}

static int split_parent_name(const char* full, char* parent, char* name) {
    const char* slash = 0;
    if (!full || full[0] != '/') return -1;
    for (const char* p = full; *p; ++p) if (*p == '/') slash = p;
    if (!slash || !slash[1]) return -1;
    size_t plen = (size_t)(slash - full);
    size_t nlen = strlen(slash + 1);
    if (nlen == 0 || nlen >= PATH_MAX) return -1;
    if (plen == 0) { parent[0] = '/'; parent[1] = '\0'; }
    else { memcpy(parent, full, plen); parent[plen] = '\0'; }
    memcpy(name, slash + 1, nlen + 1);
    return 0;
}

static void cmd_ls(const char* cwd, const char* arg) {
    char path[PATH_MAX];
    ntux_dirent_t ents[LS_MAX];
    uint64_t count = 0;
    if (!arg) arg = ".";
    if (normalize_path(cwd, arg, path) != 0) { puts("ls: invalid path"); return; }
    if (sys_fs_list_dir(path, ents, LS_MAX, &count) != 0) { puts("ls: failed"); return; }
    if (count > LS_MAX) count = LS_MAX;
    for (uint64_t i = 0; i < count; ++i) {
        if (ents[i].is_dir) printf("[D] %s\n", ents[i].name);
        else printf("[F] %s (%llu)\n", ents[i].name, ents[i].size);
    }
}

static void cmd_cat(const char* cwd, const char* arg) {
    char path[PATH_MAX];
    char buf[CAT_MAX + 1];
    uint64_t out_len = 0;
    if (!arg) { puts("usage: cat <path>"); return; }
    if (normalize_path(cwd, arg, path) != 0) { puts("cat: invalid path"); return; }
    if (sys_fs_read_file(path, buf, CAT_MAX, &out_len) != 0) { puts("cat: failed"); return; }
    if (out_len > CAT_MAX) out_len = CAT_MAX;
    buf[out_len] = '\0';
    sys_write(buf, (size_t)out_len);
    if (out_len == 0 || buf[out_len - 1] != '\n') putchar('\n');
}

static void cmd_cd(char* cwd, const char* arg) {
    char path[PATH_MAX];
    ntux_dirent_t probe[1];
    uint64_t n = 0;
    if (!arg) arg = "/";
    if (normalize_path(cwd, arg, path) != 0) { puts("cd: invalid path"); return; }
    if (sys_fs_list_dir(path, probe, 1, &n) != 0) { puts("cd: no such directory"); return; }
    strcpy(cwd, path);
}

static int task_is_alive(int tid) {
    ntux_task_info_t tasks[64];
    uint64_t count = 0;
    if (sys_task_list(tasks, 64, &count) != 0) return 0;
    for (uint64_t i = 0; i < count && i < 64; ++i) {
        if (!tasks[i].active) continue;
        if ((int)tasks[i].id == tid) return 1;
    }
    return 0;
}

static void wait_task_exit(int tid) {
    if (tid < 0) return;
    while (task_is_alive(tid)) sys_yield();
}

static void print_prompt(const char* cwd) {
    ntux_time_t now;
    int has_time = (sys_get_time(&now) == 0);
    printf("+--ntux @ %s :: ", cwd);
    if (has_time) {
        print_two_digits(now.hour); putchar(':');
        print_two_digits(now.minute); putchar(':');
        print_two_digits(now.second);
    } else {
        printf("tick %llu", sys_get_ticks());
    }
    printf("\n+-> ");
}

void ntux_user_entry(void) {
    char line[LINE_MAX];
    char* argv[ARGV_MAX];
    int argc;
    char cwd[PATH_MAX];

    if (window_init() != 0) {
        puts("[terminal] window_init failed");
        sys_exit(1);
    }

    strcpy(cwd, "/");
    puts("+-------------------------- NTux Terminal -------------------------+");
    puts("| profile: NTux-OS          renderer: desktop window  status: live |");
    puts("+------------------------------------------------------------------+");
    puts(" help  clear  ls  cd  cat  run  echo  exit");
    puts("+------------------------------------------------------------------+");

    for (;;) {
        if (sys_console_claim() != 0) {
            sys_yield();
            continue;
        }
        print_prompt(cwd);
        if (readline(line, sizeof(line)) == 0) continue;

        argc = split_args(line, argv);
        if (argc <= 0) continue;

        if (strcmp(argv[0], "help") == 0) {
            puts("Commands: help clear ls cd cat run echo exit");
            puts("  ls [path]   - list directory");
            puts("  cd <path>   - change directory");
            puts("  cat <path>  - print file contents");
            puts("  run <path>  - execute a program");
            puts("  echo <text> - print text");
            puts("  clear       - clear screen");
            puts("  exit        - exit terminal");
            continue;
        }
        if (strcmp(argv[0], "clear") == 0) {
            sys_write("\033[2J\033[H", 7);
            continue;
        }
        if (strcmp(argv[0], "ls") == 0) {
            cmd_ls(cwd, argc > 1 ? argv[1] : 0);
            continue;
        }
        if (strcmp(argv[0], "cd") == 0) {
            cmd_cd(cwd, argc > 1 ? argv[1] : 0);
            continue;
        }
        if (strcmp(argv[0], "cat") == 0) {
            cmd_cat(cwd, argc > 1 ? argv[1] : 0);
            continue;
        }
        if (strcmp(argv[0], "echo") == 0) {
            for (int i = 1; i < argc; ++i) {
                printf("%s", argv[i]);
                if (i + 1 < argc) putchar(' ');
            }
            putchar('\n');
            continue;
        }
        if (strcmp(argv[0], "run") == 0 && argc >= 2) {
            char path[PATH_MAX];
            if (normalize_path(cwd, argv[1], path) != 0) {
                puts("run: invalid path");
                continue;
            }
            if (sys_fs_exists(path) <= 0) {
                puts("run: file not found");
                continue;
            }
            long rc = sys_task_add(path);
            if (rc >= 0) {
                sys_yield();
                sys_yield();
                wait_task_exit((int)rc);
            } else {
                printf("run: failed (%ld)\n", rc);
            }
            continue;
        }
        if (strcmp(argv[0], "exit") == 0) {
            puts("bye");
            break;
        }
        printf("unknown: %s (type 'help')\n", argv[0]);
    }

    sys_exit(0);
}
