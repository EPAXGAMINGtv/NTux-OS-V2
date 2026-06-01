#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <args.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

char **environ = 0;

int luaopen_ntux(lua_State *L);

static void print_lua_error(lua_State *L, const char *context) {
    const char *msg = lua_tostring(L, -1);
    fprintf(stderr, "lua: %s: %s\n", context, msg ? msg : "(no error)");
    lua_pop(L, 1);
}

static int lua_do_buffer(lua_State *L, const char *buf, size_t sz, const char *name) {
    int status = luaL_loadbuffer(L, buf, sz, name);
    if (status != LUA_OK) {
        print_lua_error(L, "compile");
        return status;
    }
    status = lua_pcall(L, 0, LUA_MULTRET, 0);
    if (status != LUA_OK) {
        print_lua_error(L, "runtime");
    }
    return status;
}

static void do_repl(lua_State *L) {
    char line[4096];
    int line_len = 0;
    printf("Lua 5.4 interactive (NTux-OS)\n");
    for (;;) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) {
            sys_yield();
            continue;
        }
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (n == 0) continue;
        if (strcmp(line, "exit") == 0) break;
        lua_do_buffer(L, line, n, "=stdin");
    }
}

static void do_file(lua_State *L, const char *path, int argc, char **argv) {
    lua_newtable(L);
    lua_pushstring(L, path);
    lua_rawseti(L, -2, 0);
    for (int i = 1; i < argc; i++) {
        lua_pushstring(L, argv[i]);
        lua_rawseti(L, -2, i);
    }
    lua_setglobal(L, "arg");
    if (luaL_dofile(L, path) != LUA_OK) {
        print_lua_error(L, path);
    }
}

void ntux_user_entry(void) {
    ntux_args_init();
    int argc = ntux_argc();
    char **argv = ntux_argv();
    lua_State *L = luaL_newstate();
    if (!L) {
        fprintf(stderr, "lua: failed to create state\n");
        sys_exit(1);
    }
    luaL_openlibs(L);
    luaL_requiref(L, "ntux", luaopen_ntux, 1);
    lua_pop(L, 1);
    if (argc >= 2 && argv && argv[1] && argv[1][0]) {
        do_file(L, argv[1], argc - 1, argv + 1);
    } else {
        do_repl(L);
    }
    lua_close(L);
    sys_exit(0);
}
