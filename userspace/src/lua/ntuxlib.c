#include <stdint.h>
#include <string.h>
#include <syscall.h>
#include <window.h>

#include "lua.h"
#include "lauxlib.h"

static int g_window_inited = 0;

static int l_ntux_ticks(lua_State *L) {
    lua_pushinteger(L, (lua_Integer)sys_get_ticks());
    return 1;
}

static int l_ntux_sleep(lua_State *L) {
    lua_Integer ms = luaL_checkinteger(L, 1);
    if (ms < 0) ms = 0;
    uint64_t hz = (uint64_t)sys_get_timer_hz();
    if (hz == 0) hz = 1000u;
    uint64_t ticks = ((uint64_t)ms * hz) / 1000u;
    if (ticks == 0) ticks = 1;
    sys_wait_ticks(ticks);
    return 0;
}

static int l_win_init(lua_State *L) {
    if (!g_window_inited) {
        g_window_inited = (window_init() == 0);
    }
    lua_pushboolean(L, g_window_inited ? 1 : 0);
    return 1;
}

static int l_win_create(lua_State *L) {
    lua_Integer id = luaL_optinteger(L, 1, 0);
    int x = (int)luaL_optinteger(L, 2, 120);
    int y = (int)luaL_optinteger(L, 3, 90);
    int w = (int)luaL_optinteger(L, 4, 480);
    int h = (int)luaL_optinteger(L, 5, 320);
    const char *title = luaL_optstring(L, 6, "Lua Window");
    if (!g_window_inited) {
        g_window_inited = (window_init() == 0);
    }
    if (id == 0) {
        id = (lua_Integer)(0x4C554100u | (sys_get_ticks() & 0xFFFFu));
    }
    if (window_create((window_t)id, x, y, w, h, 0xFF0B1118u, title) != 0) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, id);
    return 1;
}

static int l_win_show(lua_State *L) {
    window_t id = (window_t)luaL_checkinteger(L, 1);
    int v = (int)luaL_optinteger(L, 2, 1);
    lua_pushboolean(L, window_show(id, v) == 0);
    return 1;
}

static int l_win_focus(lua_State *L) {
    window_t id = (window_t)luaL_checkinteger(L, 1);
    lua_pushboolean(L, window_focus(id) == 0);
    return 1;
}

static int l_win_clear(lua_State *L) {
    window_t id = (window_t)luaL_checkinteger(L, 1);
    uint32_t color = (uint32_t)luaL_optinteger(L, 2, 0xFF0B1118u);
    lua_pushboolean(L, window_clear(id, color) == 0);
    return 1;
}

static int l_win_rect(lua_State *L) {
    window_t id = (window_t)luaL_checkinteger(L, 1);
    int x = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);
    int w = (int)luaL_checkinteger(L, 4);
    int h = (int)luaL_checkinteger(L, 5);
    uint32_t color = (uint32_t)luaL_checkinteger(L, 6);
    int filled = (int)luaL_optinteger(L, 7, 1);
    lua_pushboolean(L, window_draw_rect(id, x, y, w, h, color, filled) == 0);
    return 1;
}

static int l_win_line(lua_State *L) {
    window_t id = (window_t)luaL_checkinteger(L, 1);
    int x0 = (int)luaL_checkinteger(L, 2);
    int y0 = (int)luaL_checkinteger(L, 3);
    int x1 = (int)luaL_checkinteger(L, 4);
    int y1 = (int)luaL_checkinteger(L, 5);
    uint32_t color = (uint32_t)luaL_checkinteger(L, 6);
    lua_pushboolean(L, window_draw_line(id, x0, y0, x1, y1, color) == 0);
    return 1;
}

static int l_win_button(lua_State *L) {
    window_t id = (window_t)luaL_checkinteger(L, 1);
    int x = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);
    int w = (int)luaL_checkinteger(L, 4);
    int h = (int)luaL_checkinteger(L, 5);
    const char *text = luaL_optstring(L, 6, "");
    int kind = (int)luaL_optinteger(L, 7, WINDOW_BUTTON_PRIMARY);
    lua_pushboolean(L, window_draw_button(id, x, y, w, h, text, kind) == 0);
    return 1;
}

static int l_win_image(lua_State *L) {
    window_t id = (window_t)luaL_checkinteger(L, 1);
    const char *path = luaL_checkstring(L, 2);
    int ch = (int)luaL_optinteger(L, 3, 3);
    lua_pushboolean(L, window_set_image(id, path, ch) == 0);
    return 1;
}

static int l_win_move(lua_State *L) {
    window_t id = (window_t)luaL_checkinteger(L, 1);
    int x = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);
    lua_pushboolean(L, window_move(id, x, y) == 0);
    return 1;
}

static int l_win_resize(lua_State *L) {
    window_t id = (window_t)luaL_checkinteger(L, 1);
    int w = (int)luaL_checkinteger(L, 2);
    int h = (int)luaL_checkinteger(L, 3);
    lua_pushboolean(L, window_resize(id, w, h) == 0);
    return 1;
}

static int l_win_set_rect(lua_State *L) {
    window_t id = (window_t)luaL_checkinteger(L, 1);
    int x = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);
    int w = (int)luaL_checkinteger(L, 4);
    int h = (int)luaL_checkinteger(L, 5);
    lua_pushboolean(L, window_set_rect(id, x, y, w, h) == 0);
    return 1;
}

static int l_win_minimize(lua_State *L) {
    window_t id = (window_t)luaL_checkinteger(L, 1);
    lua_pushboolean(L, window_minimize(id) == 0);
    return 1;
}

static int l_win_toggle_max(lua_State *L) {
    window_t id = (window_t)luaL_checkinteger(L, 1);
    lua_pushboolean(L, window_toggle_maximize(id) == 0);
    return 1;
}

static int l_win_text(lua_State *L) {
    window_t id = (window_t)luaL_checkinteger(L, 1);
    int x = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);
    uint32_t color = (uint32_t)luaL_checkinteger(L, 4);
    const char *text = luaL_checkstring(L, 5);
    lua_pushboolean(L, window_draw_text(id, x, y, color, text) == 0);
    return 1;
}

static int l_win_present(lua_State *L) {
    window_t id = (window_t)luaL_checkinteger(L, 1);
    lua_pushboolean(L, window_present(id) == 0);
    return 1;
}

static int l_win_should_close(lua_State *L) {
    window_t id = (window_t)luaL_checkinteger(L, 1);
    lua_pushboolean(L, window_should_close(id) ? 1 : 0);
    return 1;
}

static int l_win_input(lua_State *L) {
    window_t id = (window_t)luaL_checkinteger(L, 1);
    window_input_state_t st;
    memset(&st, 0, sizeof(st));
    if (window_get_input_state(id, &st) != 0) {
        lua_pushnil(L);
        return 1;
    }
    lua_newtable(L);
    lua_pushinteger(L, (lua_Integer)st.window_id); lua_setfield(L, -2, "window_id");
    lua_pushboolean(L, st.focused ? 1 : 0); lua_setfield(L, -2, "focused");
    lua_pushinteger(L, st.win_x); lua_setfield(L, -2, "win_x");
    lua_pushinteger(L, st.win_y); lua_setfield(L, -2, "win_y");
    lua_pushinteger(L, st.win_w); lua_setfield(L, -2, "win_w");
    lua_pushinteger(L, st.win_h); lua_setfield(L, -2, "win_h");
    lua_pushinteger(L, st.mouse_x); lua_setfield(L, -2, "mouse_x");
    lua_pushinteger(L, st.mouse_y); lua_setfield(L, -2, "mouse_y");
    lua_pushboolean(L, st.mouse_left ? 1 : 0); lua_setfield(L, -2, "mouse_left");
    lua_pushboolean(L, st.mouse_right ? 1 : 0); lua_setfield(L, -2, "mouse_right");
    lua_pushboolean(L, st.mouse_middle ? 1 : 0); lua_setfield(L, -2, "mouse_middle");
    lua_pushinteger(L, st.mouse_scroll); lua_setfield(L, -2, "mouse_scroll");
    lua_pushboolean(L, st.close_requested ? 1 : 0); lua_setfield(L, -2, "close_requested");
    return 1;
}

static uint8_t g_key_last[128];

static int l_input_key(lua_State *L) {
    int sc = (int)luaL_checkinteger(L, 1);
    if (sc < 0 || sc > 127) sc = 0;
    lua_pushboolean(L, sys_kbd_is_pressed((uint8_t)sc) > 0);
    return 1;
}

static int l_input_key_edge(lua_State *L) {
    int sc = (int)luaL_checkinteger(L, 1);
    if (sc < 0 || sc > 127) sc = 0;
    int now = (sys_kbd_is_pressed((uint8_t)sc) > 0) ? 1 : 0;
    int pressed = (now && !g_key_last[sc]) ? 1 : 0;
    g_key_last[sc] = (uint8_t)now;
    lua_pushboolean(L, pressed);
    return 1;
}

static int l_fs_exists(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    lua_pushboolean(L, sys_fs_exists(path) > 0);
    return 1;
}

static int l_fs_read(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    uint64_t len = 0;
    if (sys_fs_read_file(path, 0, 0, &len) != 0 || len == 0 || len > (256u * 1024u)) {
        lua_pushnil(L);
        return 1;
    }
    luaL_Buffer b;
    char *buf = luaL_buffinitsize(L, &b, (size_t)len);
    if (!buf) {
        lua_pushnil(L);
        return 1;
    }
    if (sys_fs_read_file(path, buf, len, &len) != 0) {
        lua_pushnil(L);
        return 1;
    }
    luaL_pushresultsize(&b, (size_t)len);
    return 1;
}

static int l_fs_write(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    size_t len = 0;
    const char *data = luaL_checklstring(L, 2, &len);
    if (sys_fs_write_file(path, data, (uint64_t)len) == 0) {
        lua_pushboolean(L, 1);
        return 1;
    }
    const char *slash = 0;
    for (const char *p = path; *p; ++p) {
        if (*p == '/') slash = p;
    }
    if (slash && slash != path) {
        char parent[128];
        char name[64];
        size_t plen = (size_t)(slash - path);
        size_t nlen = strlen(slash + 1);
        if (plen < sizeof(parent) && nlen < sizeof(name)) {
            memcpy(parent, path, plen);
            parent[plen] = '\0';
            memcpy(name, slash + 1, nlen + 1u);
            if (sys_fs_create_file(parent, name, data, (uint64_t)len) == 0) {
                lua_pushboolean(L, 1);
                return 1;
            }
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

static int l_fs_list(lua_State *L) {
    const char *path = luaL_optstring(L, 1, "/");
    ntux_dirent_t ents[64];
    uint64_t count = 0;
    if (sys_fs_list_dir(path, ents, 64, &count) != 0) {
        lua_pushnil(L);
        return 1;
    }
    if (count > 64) count = 64;
    lua_newtable(L);
    for (uint64_t i = 0; i < count; ++i) {
        lua_newtable(L);
        lua_pushstring(L, ents[i].name);
        lua_setfield(L, -2, "name");
        lua_pushboolean(L, ents[i].is_dir ? 1 : 0);
        lua_setfield(L, -2, "is_dir");
        lua_pushinteger(L, (lua_Integer)ents[i].size);
        lua_setfield(L, -2, "size");
        lua_rawseti(L, -2, (lua_Integer)(i + 1));
    }
    return 1;
}

static const luaL_Reg ntux_win_lib[] = {
    {"init", l_win_init},
    {"create", l_win_create},
    {"show", l_win_show},
    {"focus", l_win_focus},
    {"clear", l_win_clear},
    {"rect", l_win_rect},
    {"line", l_win_line},
    {"button", l_win_button},
    {"image", l_win_image},
    {"move", l_win_move},
    {"resize", l_win_resize},
    {"set_rect", l_win_set_rect},
    {"minimize", l_win_minimize},
    {"toggle_maximize", l_win_toggle_max},
    {"text", l_win_text},
    {"present", l_win_present},
    {"input", l_win_input},
    {"should_close", l_win_should_close},
    {NULL, NULL}
};

static const luaL_Reg ntux_input_lib[] = {
    {"key", l_input_key},
    {"key_edge", l_input_key_edge},
    {NULL, NULL}
};

static const luaL_Reg ntux_fs_lib[] = {
    {"exists", l_fs_exists},
    {"read", l_fs_read},
    {"write", l_fs_write},
    {"list", l_fs_list},
    {NULL, NULL}
};

static const luaL_Reg ntux_lib[] = {
    {"ticks", l_ntux_ticks},
    {"sleep_ms", l_ntux_sleep},
    {NULL, NULL}
};

int luaopen_ntux(lua_State *L) {
    luaL_newlib(L, ntux_lib);
    lua_newtable(L);
    luaL_setfuncs(L, ntux_win_lib, 0);
    lua_setfield(L, -2, "window");
    lua_newtable(L);
    luaL_setfuncs(L, ntux_input_lib, 0);
    lua_setfield(L, -2, "input");
    lua_newtable(L);
    luaL_setfuncs(L, ntux_fs_lib, 0);
    lua_setfield(L, -2, "fs");
    return 1;
}
