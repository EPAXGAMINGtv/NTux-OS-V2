#include <syscall.h>
#include <window.h>
#include <stdio.h>
#include <string.h>

#define MAX_TABS 100
#define URL_MAX 240
#define CONTENT_MAX 4096
#define LINE_MAX 128
#define LINE_CAP 256

typedef struct {
    char title[64];
    char url[URL_MAX];
    char content[CONTENT_MAX];
    char lines[LINE_CAP][LINE_MAX];
    uint32_t line_color[LINE_CAP];
    int line_count;
    int is_html;
    int loading;
} browser_tab_t;

static browser_tab_t g_tabs[MAX_TABS];
static int g_tab_count = 1;
static int g_tab_active = 0;
static int g_addr_focus = 0;
static char g_addr_input[URL_MAX] = "";
static uint8_t g_key_last[128];

static int key_edge(int sc) {
    int now = sys_kbd_is_pressed((uint8_t)sc) ? 1 : 0;
    int edge = (now && !g_key_last[sc]) ? 1 : 0;
    g_key_last[sc] = (uint8_t)now;
    return edge;
}

static int text_hit(int mx, int my, int x, int y, int w, int h) {
    return (mx >= x && my >= y && mx < x + w && my < y + h);
}

static void tab_set_title(browser_tab_t* tab, const char* s);
static void tab_set_content(browser_tab_t* tab, const char* s);

static int is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char lower_char(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + 32);
    return c;
}

static int str_starts_with_ci(const char* s, const char* pfx) {
    if (!s || !pfx) return 0;
    for (size_t i = 0; pfx[i]; ++i) {
        if (lower_char(s[i]) != lower_char(pfx[i])) return 0;
    }
    return 1;
}

static const char* skip_spaces(const char* s) {
    while (s && *s && is_ws(*s)) ++s;
    return s;
}

static uint32_t parse_hex_color(const char* s, uint32_t fallback) {
    if (!s || s[0] != '#') return fallback;
    unsigned v = 0;
    int n = 0;
    for (int i = 1; s[i] && n < 6; ++i) {
        char c = s[i];
        unsigned d;
        if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (unsigned)(c - 'A' + 10);
        else break;
        v = (v << 4) | d;
        n++;
    }
    if (n == 6) return 0xFF000000u | v;
    return fallback;
}

static uint32_t parse_tag_color(const char* tag, uint32_t fallback) {
    const char* p = tag;
    while (p && *p) {
        if (str_starts_with_ci(p, "color")) {
            const char* eq = strchr(p, '=');
            if (eq) {
                eq++;
                if (*eq == '"' || *eq == '\'') eq++;
                return parse_hex_color(eq, fallback);
            }
        }
        if (str_starts_with_ci(p, "style")) {
            const char* eq = strchr(p, '=');
            if (eq) {
                eq++;
                if (*eq == '"' || *eq == '\'') eq++;
                const char* col = strstr(eq, "color");
                if (col) {
                    const char* hash = strchr(col, '#');
                    if (hash) return parse_hex_color(hash, fallback);
                }
            }
        }
        ++p;
    }
    return fallback;
}

static int html_is(const char* s) {
    if (!s) return 0;
    return (strstr(s, "<html") != 0) || (strstr(s, "<!DOCTYPE") != 0) || (strstr(s, "<body") != 0);
}

static void line_push(browser_tab_t* tab, const char* text, uint32_t color) {
    if (!tab || !text || tab->line_count >= LINE_CAP) return;
    strncpy(tab->lines[tab->line_count], text, LINE_MAX - 1);
    tab->lines[tab->line_count][LINE_MAX - 1] = '\0';
    tab->line_color[tab->line_count] = color;
    tab->line_count++;
}

static void html_render(browser_tab_t* tab) {
    if (!tab) return;
    tab->line_count = 0;
    tab->is_html = 0;
    if (!html_is(tab->content)) {
        tab->is_html = 0;
        return;
    }
    tab->is_html = 1;

    uint32_t base_color = 0xFFE6F1FFu;
    uint32_t cur_color = base_color;
    uint32_t color_stack[8];
    int color_sp = 0;
    int in_script = 0;
    int in_style = 0;
    int in_title = 0;
    char title_buf[64] = "";
    size_t title_len = 0;

    char line[LINE_MAX];
    int lp = 0;

    const char* s = tab->content;
    for (size_t i = 0; s[i]; ++i) {
        char c = s[i];
        if (c == '<') {
            const char* tag_start = s + i + 1;
            const char* tag_end = strchr(tag_start, '>');
            if (!tag_end) break;
            size_t tag_len = (size_t)(tag_end - tag_start);
            char tag[96];
            if (tag_len >= sizeof(tag)) tag_len = sizeof(tag) - 1;
            memcpy(tag, tag_start, tag_len);
            tag[tag_len] = '\0';
            const char* t = skip_spaces(tag);
            int closing = 0;
            if (*t == '/') { closing = 1; t++; }

            if (str_starts_with_ci(t, "script")) {
                in_script = closing ? 0 : 1;
            } else if (str_starts_with_ci(t, "style")) {
                in_style = closing ? 0 : 1;
            } else if (str_starts_with_ci(t, "title")) {
                if (closing) {
                    in_title = 0;
                    if (title_buf[0]) tab_set_title(tab, title_buf);
                } else {
                    in_title = 1;
                    title_len = 0;
                    title_buf[0] = '\0';
                }
            }

            if (!in_script && !in_style) {
                if (!closing) {
                    if (str_starts_with_ci(t, "br") || str_starts_with_ci(t, "p") || str_starts_with_ci(t, "div")) {
                        line[lp] = '\0';
                        if (lp > 0) line_push(tab, line, cur_color);
                        lp = 0;
                    } else if (str_starts_with_ci(t, "h1") || str_starts_with_ci(t, "h2") || str_starts_with_ci(t, "h3")) {
                        line[lp] = '\0';
                        if (lp > 0) line_push(tab, line, cur_color);
                        lp = 0;
                        if (color_sp < 8) color_stack[color_sp++] = cur_color;
                        cur_color = 0xFF4BB3F5u;
                        line_push(tab, "", cur_color);
                    } else if (str_starts_with_ci(t, "a") || str_starts_with_ci(t, "font") || str_starts_with_ci(t, "span")) {
                        if (color_sp < 8) color_stack[color_sp++] = cur_color;
                        cur_color = parse_tag_color(t, 0xFF4BB3F5u);
                    } else if (str_starts_with_ci(t, "b") || str_starts_with_ci(t, "strong")) {
                        if (color_sp < 8) color_stack[color_sp++] = cur_color;
                        cur_color = 0xFFFFF0C0u;
                    }
                } else {
                    if (str_starts_with_ci(t, "a") || str_starts_with_ci(t, "font") || str_starts_with_ci(t, "span") ||
                        str_starts_with_ci(t, "b") || str_starts_with_ci(t, "strong") ||
                        str_starts_with_ci(t, "h1") || str_starts_with_ci(t, "h2") || str_starts_with_ci(t, "h3")) {
                        if (color_sp > 0) cur_color = color_stack[--color_sp];
                    }
                    if (str_starts_with_ci(t, "p") || str_starts_with_ci(t, "div") || str_starts_with_ci(t, "h1") ||
                        str_starts_with_ci(t, "h2") || str_starts_with_ci(t, "h3")) {
                        line[lp] = '\0';
                        if (lp > 0) line_push(tab, line, cur_color);
                        lp = 0;
                        line_push(tab, "", cur_color);
                    }
                }
            }

            i = (size_t)(tag_end - s);
            continue;
        }

        if (in_script || in_style) continue;

        if (in_title) {
            if (title_len + 1 < sizeof(title_buf)) {
                title_buf[title_len++] = c;
                title_buf[title_len] = '\0';
            }
            continue;
        }

        if (c == '\n' || c == '\r') {
            if (lp > 0) {
                line[lp] = '\0';
                line_push(tab, line, cur_color);
                lp = 0;
            }
            continue;
        }
        if (lp < LINE_MAX - 1) {
            line[lp++] = c;
        } else {
            line[lp] = '\0';
            line_push(tab, line, cur_color);
            lp = 0;
        }
    }
    if (lp > 0) {
        line[lp] = '\0';
        line_push(tab, line, cur_color);
    }
    if (tab->line_count == 0) {
        line_push(tab, "(empty document)", base_color);
    }
}

static void tab_set_title(browser_tab_t* tab, const char* s) {
    if (!tab) return;
    strncpy(tab->title, s ? s : "Tab", sizeof(tab->title) - 1);
    tab->title[sizeof(tab->title) - 1] = '\0';
}

static void tab_set_content(browser_tab_t* tab, const char* s) {
    if (!tab) return;
    strncpy(tab->content, s ? s : "", sizeof(tab->content) - 1);
    tab->content[sizeof(tab->content) - 1] = '\0';
    html_render(tab);
}

static void tab_navigate(browser_tab_t* tab, const char* url) {
    if (!tab || !url || !url[0]) return;
    strncpy(tab->url, url, sizeof(tab->url) - 1);
    tab->url[sizeof(tab->url) - 1] = '\0';
    tab->loading = 1;

    char result[CONTENT_MAX];
    result[0] = '\0';

    if (strncmp(url, "file://", 7) == 0 || url[0] == '/') {
        const char* path = (url[0] == '/') ? url : (url + 7);
        uint64_t len = 0;
        if (sys_fs_read_file(path, 0, 0, &len) == 0 && len > 0) {
            if (len >= CONTENT_MAX) len = CONTENT_MAX - 1;
            if (sys_fs_read_file(path, result, len, &len) == 0) {
                result[len] = '\0';
                tab_set_title(tab, path);
                tab_set_content(tab, result);
                tab->loading = 0;
                return;
            }
        }
        tab_set_title(tab, "File Error");
        tab_set_content(tab, "Could not read file.");
    } else {
        if (sys_net_http_get(url, result, sizeof(result)) == 0 && result[0]) {
            tab_set_title(tab, url);
            tab_set_content(tab, result);
        } else {
            snprintf(result, sizeof(result),
                     "HTTP failed or HTTPS not supported yet.\n\n"
                     "Try: http://example.com or a local file.\nURL: %s",
                     url);
            tab_set_title(tab, "Network Error");
            tab_set_content(tab, result);
        }
    }
    tab->loading = 0;
}

static void browser_init_tabs(void) {
    memset(g_tabs, 0, sizeof(g_tabs));
    tab_set_title(&g_tabs[0], "New Tab");
    tab_set_content(&g_tabs[0], "Enter a URL and press Enter.\n\nHTML/CSS/JS engine is in progress.");
    g_tabs[0].url[0] = '\0';
}

static void draw_ui(window_t win, const window_input_state_t* st, int hover_tab, int hover_btn, int hover_addr) {
    (void)st;
    uint32_t bg = 0xFF0C131Au;
    uint32_t bar = 0xFF141D28u;
    uint32_t accent = 0xFF4BB3F5u;
    uint32_t text = 0xFFE6F1FFu;
    uint32_t dim = 0xFF9FB6CCu;

    window_clear(win, bg);

    int x = 10;
    int y = 8;
    int tab_h = 22;
    for (int i = 0; i < g_tab_count; ++i) {
        int w = 120;
        uint32_t c = (i == g_tab_active) ? accent : bar;
        uint32_t t = (i == g_tab_active) ? 0xFF091018u : text;
        if (hover_tab == i && i != g_tab_active) c = 0xFF2D3F54u;
        window_draw_rect(win, x, y, w, tab_h, c, 1);
        window_draw_text(win, x + 8, y + 6, t, g_tabs[i].title[0] ? g_tabs[i].title : "Tab");
        x += w + 6;
    }

    int btn_x = x + 4;
    window_draw_button(win, btn_x, y, 26, tab_h, "+", WINDOW_BUTTON_SECONDARY);
    if (hover_btn) {
        window_draw_rect(win, btn_x, y, 26, tab_h, 0x5522AAFFu, 0);
    }

    int addr_y = y + tab_h + 10;
    int addr_x = 10;
    int addr_w = 780;
    int addr_h = 24;
    uint32_t addr_c = g_addr_focus ? 0xFF1F2B3Bu : bar;
    window_draw_rect(win, addr_x, addr_y, addr_w, addr_h, addr_c, 1);
    window_draw_rect(win, addr_x, addr_y, addr_w, addr_h, dim, 0);
    window_draw_text(win, addr_x + 8, addr_y + 7, text, g_addr_input[0] ? g_addr_input : "Enter URL...");

    int view_x = 10;
    int view_y = addr_y + addr_h + 10;
    int view_w = 780;
    int view_h = 500;
    window_draw_rect(win, view_x, view_y, view_w, view_h, bar, 1);
    window_draw_rect(win, view_x, view_y, view_w, view_h, dim, 0);

    const browser_tab_t* tab = &g_tabs[g_tab_active];
    int line = 0;
    int col_y = view_y + 8;
    if (tab->line_count > 0) {
        for (int i = 0; i < tab->line_count; ++i) {
            int py = col_y + line * 10;
            if (py > view_y + view_h - 14) break;
            uint32_t lc = tab->line_color[i] ? tab->line_color[i] : text;
            window_draw_text(win, view_x + 8, py, lc, tab->lines[i]);
            line++;
        }
    } else {
        window_draw_text(win, view_x + 8, col_y, text, "(empty)");
    }

    if (g_tabs[g_tab_active].loading) {
        window_draw_text(win, view_x + view_w - 80, view_y + view_h - 16, accent, "Loading...");
    }

    if (hover_addr) {
        window_draw_rect(win, addr_x, addr_y, addr_w, addr_h, 0x55FFFFFFu, 0);
    }

    window_present(win);
}

void ntux_user_entry(void) {
    window_t win = 0x42524F575345525Full; /* "BROWSER" */
    if (window_init() != 0 || window_create(win, 60, 60, 820, 600, 0xFF0C131Au, "Browser") != 0) {
        sys_exit(1);
    }
    (void)window_set_icon(win, "/boot/res/icons/browser.bmp");

    browser_init_tabs();
    strncpy(g_addr_input, g_tabs[0].url, sizeof(g_addr_input) - 1);
    g_addr_input[sizeof(g_addr_input) - 1] = '\0';

    int last_left = 0;
    for (;;) {
        if (window_should_close(win)) break;
        if (key_edge(0x01)) break;

        window_input_state_t st;
        memset(&st, 0, sizeof(st));
        window_get_input_state(win, &st);

        int mx = st.mouse_x;
        int my = st.mouse_y;

        int hover_tab = -1;
        int tab_x = 10;
        int tab_y = 8;
        int tab_h = 22;
        for (int i = 0; i < g_tab_count; ++i) {
            if (text_hit(mx, my, tab_x, tab_y, 120, tab_h)) { hover_tab = i; break; }
            tab_x += 126;
        }
        int btn_x = tab_x + 4;
        int hover_btn = text_hit(mx, my, btn_x, tab_y, 26, tab_h);

        int addr_x = 10;
        int addr_y = tab_y + tab_h + 10;
        int addr_w = 780;
        int addr_h = 24;
        int hover_addr = text_hit(mx, my, addr_x, addr_y, addr_w, addr_h);

        if (st.mouse_left && !last_left) {
            if (hover_tab >= 0) {
                g_tab_active = hover_tab;
                strncpy(g_addr_input, g_tabs[g_tab_active].url, sizeof(g_addr_input) - 1);
                g_addr_input[sizeof(g_addr_input) - 1] = '\0';
            } else if (hover_btn) {
                if (g_tab_count < MAX_TABS) {
                    g_tab_count++;
                    g_tab_active = g_tab_count - 1;
                    tab_set_title(&g_tabs[g_tab_active], "New Tab");
                    g_tabs[g_tab_active].url[0] = '\0';
                    tab_set_content(&g_tabs[g_tab_active], "Enter a URL and press Enter.");
                    g_addr_input[0] = '\0';
                }
            } else if (hover_addr) {
                g_addr_focus = 1;
            } else {
                g_addr_focus = 0;
            }
        }

        long ch = sys_getchar();
        if (g_addr_focus && ch > 0) {
            if (ch == '\n' || ch == '\r') {
                tab_navigate(&g_tabs[g_tab_active], g_addr_input);
            } else if (ch == 8 || ch == 127) {
                size_t len = strlen(g_addr_input);
                if (len > 0) g_addr_input[len - 1] = '\0';
            } else if (ch >= 32 && ch <= 126) {
                size_t len = strlen(g_addr_input);
                if (len + 1 < sizeof(g_addr_input)) {
                    g_addr_input[len] = (char)ch;
                    g_addr_input[len + 1] = '\0';
                }
            }
        }

        draw_ui(win, &st, hover_tab, hover_btn, hover_addr);
        last_left = st.mouse_left;
        sys_wait_ticks(1);
    }

    window_close(win);
    sys_exit(0);
}
