#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <window.h>

#define CALC_W 360
#define CALC_H 520

#define DISP_H 96
#define PAD 16
#define GAP 10

typedef struct {
    uint8_t scancode;
    char normal;
    char shifted;
} keymap_t;

static const keymap_t g_keys[] = {
    {0x02, '1', '!'}, {0x03, '2', '"'}, {0x04, '3', '#'}, {0x05, '4', '$'},
    {0x06, '5', '%'}, {0x07, '6', '&'}, {0x08, '7', '/'}, {0x09, '8', '('},
    {0x0A, '9', ')'}, {0x0B, '0', '='}, {0x0C, '-', '_'}, {0x0D, '+', '*'},
    {0x33, ',', '<'}, {0x34, '.', '>'}, {0x35, '/', '?'} , {0x39, ' ', ' '}
};

static char g_expr[128];
static char g_result[64];
static int g_expr_len = 0;
static int g_after_eval = 0;
static uint8_t g_key_last[128];
static int g_last_left = 0;

static int key_edge(int sc) {
    int now = (sys_kbd_is_pressed((uint8_t)sc) > 0) ? 1 : 0;
    int pressed = (now && !g_key_last[sc]) ? 1 : 0;
    g_key_last[sc] = (uint8_t)now;
    return pressed;
}

static void expr_clear(void) {
    g_expr[0] = '\0';
    g_expr_len = 0;
    g_result[0] = '\0';
    g_after_eval = 0;
}

static void expr_backspace(void) {
    if (g_expr_len <= 0) return;
    g_expr_len--;
    g_expr[g_expr_len] = '\0';
    g_result[0] = '\0';
}

static int expr_append(char c) {
    if (g_expr_len + 1 >= (int)sizeof(g_expr)) return -1;
    g_expr[g_expr_len++] = c;
    g_expr[g_expr_len] = '\0';
    g_result[0] = '\0';
    return 0;
}

static void skip_ws(const char** p) {
    while (**p == ' ' || **p == '\t') (*p)++;
}

static double parse_expr(const char** p, int* ok);

static double parse_number(const char** p, int* ok) {
    const char* s = *p;
    skip_ws(&s);
    double val = 0.0;
    int saw = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10.0 + (double)(*s - '0');
        s++;
        saw = 1;
    }
    if (*s == '.') {
        s++;
        double frac = 0.1;
        while (*s >= '0' && *s <= '9') {
            val += (double)(*s - '0') * frac;
            frac *= 0.1;
            s++;
            saw = 1;
        }
    }
    if (!saw) {
        *ok = 0;
        return 0.0;
    }
    *p = s;
    *ok = 1;
    return val;
}

static double parse_factor(const char** p, int* ok) {
    skip_ws(p);
    if (**p == '+') {
        (*p)++;
        return parse_factor(p, ok);
    }
    if (**p == '-') {
        (*p)++;
        return -parse_factor(p, ok);
    }
    if (**p == '(') {
        (*p)++;
        double v = parse_expr(p, ok);
        skip_ws(p);
        if (**p != ')') {
            *ok = 0;
            return 0.0;
        }
        (*p)++;
        return v;
    }
    return parse_number(p, ok);
}

static double parse_term(const char** p, int* ok) {
    double v = parse_factor(p, ok);
    if (!*ok) return 0.0;
    for (;;) {
        skip_ws(p);
        if (**p == '*') {
            (*p)++;
            double rhs = parse_factor(p, ok);
            if (!*ok) return 0.0;
            v *= rhs;
        } else if (**p == '/') {
            (*p)++;
            double rhs = parse_factor(p, ok);
            if (!*ok || rhs == 0.0) { *ok = 0; return 0.0; }
            v /= rhs;
        } else {
            break;
        }
    }
    return v;
}

static double parse_expr(const char** p, int* ok) {
    double v = parse_term(p, ok);
    if (!*ok) return 0.0;
    for (;;) {
        skip_ws(p);
        if (**p == '+') {
            (*p)++;
            double rhs = parse_term(p, ok);
            if (!*ok) return 0.0;
            v += rhs;
        } else if (**p == '-') {
            (*p)++;
            double rhs = parse_term(p, ok);
            if (!*ok) return 0.0;
            v -= rhs;
        } else {
            break;
        }
    }
    return v;
}

static int append_u64(char* out, size_t cap, uint64_t v) {
    char tmp[32];
    size_t p = 0;
    if (cap < 2) return -1;
    if (v == 0) {
        out[0] = '0';
        out[1] = '\0';
        return 0;
    }
    while (v > 0 && p < sizeof(tmp)) {
        tmp[p++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    if (p + 1 > cap) return -1;
    for (size_t i = 0; i < p; ++i) {
        out[i] = tmp[p - 1 - i];
    }
    out[p] = '\0';
    return 0;
}

static int format_number(double v, char* out, size_t cap) {
    if (!out || cap < 2) return -1;
    if (v != v) { /* NaN */
        strncpy(out, "Error", cap - 1);
        out[cap - 1] = '\0';
        return -1;
    }
    int neg = 0;
    if (v < 0.0) { neg = 1; v = -v; }
    if (v > 9.22e18) {
        strncpy(out, "Error", cap - 1);
        out[cap - 1] = '\0';
        return -1;
    }
    uint64_t ip = (uint64_t)v;
    double frac = v - (double)ip;
    uint64_t frac_i = (uint64_t)(frac * 1000000.0 + 0.5);
    if (frac_i >= 1000000u) {
        ip += 1u;
        frac_i -= 1000000u;
    }
    char buf[96];
    char* p = buf;
    size_t left = sizeof(buf);
    if (neg) {
        if (left < 2) { out[0] = '\0'; return -1; }
        *p++ = '-'; left--;
    }
    if (append_u64(p, left, ip) != 0) { out[0] = '\0'; return -1; }
    size_t len = strlen(p);
    p += len; left -= len;
    if (frac_i != 0) {
        if (left < 2) { out[0] = '\0'; return -1; }
        *p++ = '.'; left--;
        char frac_buf[8];
        for (int i = 5; i >= 0; --i) {
            frac_buf[i] = (char)('0' + (frac_i % 10u));
            frac_i /= 10u;
        }
        int end = 5;
        while (end >= 0 && frac_buf[end] == '0') end--;
        for (int i = 0; i <= end && left > 1; ++i) {
            *p++ = frac_buf[i];
            left--;
        }
    }
    *p = '\0';
    strncpy(out, buf, cap - 1);
    out[cap - 1] = '\0';
    return 0;
}

static int eval_expr(char* out, size_t cap) {
    const char* p = g_expr;
    int ok = 1;
    double v = parse_expr(&p, &ok);
    skip_ws(&p);
    if (!ok || *p != '\0') {
        strncpy(out, "Error", cap - 1);
        out[cap - 1] = '\0';
        return -1;
    }
    return format_number(v, out, cap);
}

static void apply_input_char(char c) {
    if (g_after_eval) {
        if ((c >= '0' && c <= '9') || c == '.' || c == '(') {
            g_expr[0] = '\0';
            g_expr_len = 0;
        } else if (g_result[0]) {
            strncpy(g_expr, g_result, sizeof(g_expr) - 1);
            g_expr[sizeof(g_expr) - 1] = '\0';
            g_expr_len = (int)strlen(g_expr);
        }
        g_after_eval = 0;
    }
    (void)expr_append(c);
}

static void handle_keyboard(void) {
    if (key_edge(0x01)) sys_exit(0); /* Esc */
    if (key_edge(0x1C)) { /* Enter */
        if (g_expr_len > 0) {
            if (eval_expr(g_result, sizeof(g_result)) == 0) g_after_eval = 1;
        }
        return;
    }
    if (key_edge(0x0E)) { /* Backspace */
        expr_backspace();
        return;
    }
    if (key_edge(0x0F)) { /* Tab = clear */
        expr_clear();
        return;
    }
    int shift = (sys_kbd_is_pressed(0x2A) > 0) || (sys_kbd_is_pressed(0x36) > 0);
    for (size_t i = 0; i < sizeof(g_keys) / sizeof(g_keys[0]); ++i) {
        if (key_edge(g_keys[i].scancode)) {
            char c = shift ? g_keys[i].shifted : g_keys[i].normal;
            if (c == ',' ) c = '.';
            if (c == '=' || c == '\n') {
                if (g_expr_len > 0 && eval_expr(g_result, sizeof(g_result)) == 0) g_after_eval = 1;
            } else if ((c >= '0' && c <= '9') || c == '.' || c == '+' || c == '-' || c == '*' ||
                       c == '/' || c == '(' || c == ')') {
                apply_input_char(c);
            }
            break;
        }
    }
}

static int in_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return (x >= rx && y >= ry && x < rx + rw && y < ry + rh);
}

static void draw_display(window_t id) {
    int x = PAD;
    int y = PAD;
    int w = CALC_W - PAD * 2;
    (void)window_draw_rect(id, x, y, w, DISP_H, 0xFF0F1721u, 1);
    (void)window_draw_rect(id, x, y, w, DISP_H, 0xFF24384Du, 0);
    (void)window_draw_text(id, x + 10, y + 10, 0xFF9FC7E6u, "Calculator");
    (void)window_draw_text(id, x + 10, y + 36, 0xFFEAF4FFu, g_expr_len ? g_expr : "0");
    if (g_result[0]) {
        (void)window_draw_text(id, x + 10, y + 60, 0xFF7FB4FFu, g_result);
    }
}

static void draw_button(window_t id, int x, int y, int w, int h, const char* label, int kind) {
    (void)window_draw_button(id, x, y, w, h, label, kind);
}

static void handle_button(const char* label) {
    if (strcmp(label, "C") == 0) {
        expr_clear();
        return;
    }
    if (strcmp(label, "DEL") == 0) {
        expr_backspace();
        return;
    }
    if (strcmp(label, "=") == 0) {
        if (g_expr_len > 0 && eval_expr(g_result, sizeof(g_result)) == 0) g_after_eval = 1;
        return;
    }
    if (label[0] && !label[1]) {
        char c = label[0];
        apply_input_char(c);
    }
}

void ntux_user_entry(void) {
    window_t id = 0x43414C43554C4Cull; /* "CALCUL" */
    if (window_init() != 0 || window_create(id, 160, 120, CALC_W, CALC_H, 0xFF0B1119u, "Calculator") != 0) {
        sys_exit(1);
    }
    (void)window_set_icon(id, "/boot/res/icons/calc.bmp");
    expr_clear();

    const char* labels[5][4] = {
        {"C", "DEL", "(", ")"},
        {"7", "8", "9", "/"},
        {"4", "5", "6", "*"},
        {"1", "2", "3", "-"},
        {"0", ".", "=", "+"}
    };

    for (;;) {
        if (window_should_close(id)) break;
        handle_keyboard();

        window_input_state_t st;
        memset(&st, 0, sizeof(st));
        (void)window_get_input_state(id, &st);
        int ldown = st.mouse_left;
        int lclick = ldown && !g_last_left;

        (void)window_clear(id, 0xFF0B1119u);
        draw_display(id);

        int grid_x = PAD;
        int grid_y = PAD + DISP_H + GAP;
        int grid_w = CALC_W - PAD * 2;
        int grid_h = CALC_H - grid_y - PAD;
        int cols = 4;
        int rows = 5;
        int btn_w = (grid_w - (cols - 1) * GAP) / cols;
        int btn_h = (grid_h - (rows - 1) * GAP) / rows;

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                int bx = grid_x + c * (btn_w + GAP);
                int by = grid_y + r * (btn_h + GAP);
                const char* label = labels[r][c];
                int kind = WINDOW_BUTTON_SECONDARY;
                if (strcmp(label, "=") == 0) kind = WINDOW_BUTTON_PRIMARY;
                if (strcmp(label, "C") == 0) kind = WINDOW_BUTTON_DANGER;
                draw_button(id, bx, by, btn_w, btn_h, label, kind);
                if (lclick && in_rect(st.mouse_x, st.mouse_y, bx, by, btn_w, btn_h)) {
                    handle_button(label);
                }
            }
        }

        g_last_left = ldown;
        (void)window_present(id);
        sys_wait_ticks(1);
    }

    window_close(id);
    sys_exit(0);
}
