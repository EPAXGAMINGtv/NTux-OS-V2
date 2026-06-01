#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include <args.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

static int g_exit_requested = 0;

int luaopen_ntux(lua_State *L);

static const char *k_lua_run_paths[] = {
    "/tmp/lua.run",
    "/boot/boot/modules/lua.run",
    "/boot/modules/lua.run"
};
static const char *k_lua_arg_paths[] = {
    "/tmp/lua.args",
    "/boot/boot/modules/lua.args",
    "/boot/modules/lua.args"
};

static int lua_exit(lua_State *L) {
    (void)L;
    g_exit_requested = 1;
    return 0;
}

static void print_lua_error(lua_State *L, const char *context) {
    const char *msg = lua_tostring(L, -1);
    if (!msg) msg = "(no error message)";
    printf("[lua] %s: %s\n", context, msg);
    lua_pop(L, 1);
}

static void run_file_if_exists(lua_State *L, const char *path) {
    if (sys_fs_exists(path) != 1) return;
    if (luaL_dofile(L, path) != LUA_OK) {
        print_lua_error(L, path);
    }
}

static int read_small_file(const char *path, char *out, size_t cap) {
    if (!path || !out || cap < 2) return -1;
    uint64_t len = 0;
    if (sys_fs_read_file(path, 0, 0, &len) != 0) return -1;
    if (len == 0 || len >= (uint64_t)cap) return -1;
    if (sys_fs_read_file(path, out, cap, &len) != 0) return -1;
    out[len] = '\0';
    return 0;
}

static void trim_ascii(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0) {
        char c = s[n - 1];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            s[--n] = '\0';
        } else {
            break;
        }
    }
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') i++;
    if (i > 0) {
        memmove(s, s + i, strlen(s + i) + 1u);
    }
}

static int parse_args_line(char *line, char *argv[], int maxc) {
    int argc = 0;
    if (!line || !argv || maxc <= 0) return 0;
    char *p = line;
    while (*p && argc + 1 < maxc) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
        if (*p) {
            *p = '\0';
            p++;
        }
    }
    argv[argc] = 0;
    return argc;
}

static int try_read_tid_args(char *buf, size_t cap, char *argv[], int maxc) {
    if (!buf || cap < 2) return 0;
    long tid = sys_get_tid();
    if (tid <= 0) return 0;
    char path[64];
    int n = snprintf(path, sizeof(path), "/tmp/args.%ld", tid);
    if (n <= 0 || (size_t)n >= sizeof(path)) return 0;
    for (int attempt = 0; attempt < 50; ++attempt) {
        uint64_t len = 0;
        if (sys_fs_read_file(path, 0, 0, &len) == 0 && len > 0 && len < cap) {
            if (sys_fs_read_file(path, buf, cap, &len) == 0) {
                if (len >= cap) len = cap - 1u;
                buf[len] = '\0';
                (void)sys_fs_remove(path);
                return parse_args_line(buf, argv, maxc);
            }
        }
        sys_wait_ticks(1);
    }
    return 0;
}

static int run_script_from_runfile(lua_State *L) {
    char path[256];
    char args_line[256];
    int have_args = 0;
    for (size_t i = 0; i < (sizeof(k_lua_run_paths) / sizeof(k_lua_run_paths[0])); ++i) {
        const char *run_path = k_lua_run_paths[i];
        if (sys_fs_exists(run_path) != 1) continue;
        if (read_small_file(run_path, path, sizeof(path)) != 0) {
            (void)sys_fs_remove(run_path);
            continue;
        }
        trim_ascii(path);
        (void)sys_fs_remove(run_path);
        if (path[0] == '\0') continue;
        if (sys_fs_exists(path) != 1) {
            printf("[lua] script not found: %s\n", path);
            continue;
        }
        for (size_t a = 0; a < (sizeof(k_lua_arg_paths) / sizeof(k_lua_arg_paths[0])); ++a) {
            const char *arg_path = k_lua_arg_paths[a];
            if (sys_fs_exists(arg_path) != 1) continue;
            if (read_small_file(arg_path, args_line, sizeof(args_line)) == 0) {
                trim_ascii(args_line);
                if (args_line[0]) have_args = 1;
            }
            (void)sys_fs_remove(arg_path);
            break;
        }

        lua_newtable(L);
        lua_pushstring(L, path);
        lua_rawseti(L, -2, 0);
        if (have_args) {
            int idx = 1;
            char *p = args_line;
            while (*p) {
                while (*p == ' ' || *p == '\t') p++;
                if (!*p) break;
                char *start = p;
                while (*p && *p != ' ' && *p != '\t') p++;
                char saved = *p;
                *p = '\0';
                lua_pushstring(L, start);
                lua_rawseti(L, -2, idx++);
                *p = saved;
            }
        }
        lua_setglobal(L, "arg");
        if (luaL_dofile(L, path) != LUA_OK) {
            print_lua_error(L, path);
        }
        return 1;
    }
    return 0;
}

static void repl(lua_State *L) {
    char line[256];
    puts("[lua] REPL: tippe 'exit' zum Beenden");
    while (!g_exit_requested) {
        puts("> ");
        if (!fgets(line, sizeof(line), stdin)) {
            (void)sys_yield();
            continue;
        }
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[--n] = '\0';
        }
        if (n == 0) continue;
        if (strcmp(line, "exit") == 0) break;
        if (luaL_loadstring(L, line) != LUA_OK) {
            print_lua_error(L, "compile");
            continue;
        }
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            print_lua_error(L, "runtime");
        }
    }
}

void ntux_user_entry(void) {
    lua_State *L = luaL_newstate();
    if (!L) {
        puts("[lua] Fehler: keine Lua-VM");
        sys_exit(1);
    }
    luaL_openlibs(L);
    luaL_requiref(L, "ntux", luaopen_ntux, 1);
    lua_pop(L, 1);
    lua_pushcfunction(L, lua_exit);
    lua_setglobal(L, "exit");

    int argc = ntux_argc();
    char **argv = ntux_argv();
    if (argc >= 2 && argv && argv[1] && argv[1][0]) {
        if (strcmp(argv[1], "konsole") == 0 || strcmp(argv[1], "console") == 0) {
            while (sys_console_claim() != 0) {
                sys_yield();
            }
            puts("[lua] Konsole-Modus aktiv");
            repl(L);
            lua_close(L);
            sys_exit(0);
        }
        lua_newtable(L);
        for (int i = 0; i < argc; ++i) {
            lua_pushstring(L, argv[i] ? argv[i] : "");
            lua_rawseti(L, -2, i);
        }
        lua_setglobal(L, "arg");
        if (luaL_dofile(L, argv[1]) != LUA_OK) {
            print_lua_error(L, argv[1]);
        }
    } else {
        char arg_buf[256];
        char *arg_argv[16];
        int arg_argc = try_read_tid_args(arg_buf, sizeof(arg_buf), arg_argv, 16);
        if (arg_argc >= 2 && arg_argv[1] && arg_argv[1][0]) {
            lua_newtable(L);
            for (int i = 0; i < arg_argc; ++i) {
                lua_pushstring(L, arg_argv[i] ? arg_argv[i] : "");
                lua_rawseti(L, -2, i);
            }
            lua_setglobal(L, "arg");
            if (luaL_dofile(L, arg_argv[1]) != LUA_OK) {
                print_lua_error(L, arg_argv[1]);
            }
            lua_close(L);
            sys_exit(0);
        }
        if (run_script_from_runfile(L)) {
            lua_close(L);
            sys_exit(0);
        }
        puts("[lua] Tipp: run lua.elf konsole");
    }

    lua_close(L);
    sys_exit(0);
}
