#include <syscall.h>
#include <window.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <args.h>

#define TN_IAC  0xFF
#define TN_WILL 0xFB
#define TN_WONT 0xFC
#define TN_DO   0xFD
#define TN_DONT 0xFE
#define TN_SB   0xFA
#define TN_SE   0xF0

#define ESC_CHAR 0x1D

#define WIN_W 780
#define WIN_H 520
#define DISP_X 4
#define DISP_Y 28
#define DISP_W (WIN_W - 8)
#define DISP_H (WIN_H - 66)
#define INPUT_Y (WIN_H - 34)
#define INPUT_W (WIN_W - 100)
#define BTN_X (WIN_W - 90)
#define BTN_W 84

#define MAX_LINES 1024
#define LINE_LEN 120

static window_t g_win = 0x54454C4E4554ull;
static int g_connected = 0;
static int g_quit = 0;

// Display buffer (ring buffer of lines)
static char g_lines[MAX_LINES][LINE_LEN];
static int g_line_head = 0;
static int g_line_count = 0;
static int g_scroll = 0;

static uint8_t g_key_last[128];
static int key_edge(int sc) {
    int now = (sys_kbd_is_pressed((uint8_t)sc) > 0) ? 1 : 0;
    int pressed = (now && !g_key_last[sc]) ? 1 : 0;
    g_key_last[sc] = (uint8_t)now;
    return pressed;
}

static void push_line(const char* s) {
    if (g_line_count < MAX_LINES) g_line_count++;
    memcpy(g_lines[g_line_head], s, LINE_LEN - 1);
    g_lines[g_line_head][LINE_LEN - 1] = '\0';
    g_line_head = (g_line_head + 1) % MAX_LINES;
    if (g_scroll > 0) g_scroll++;
}

static void push_char(char c) {
    int idx = (g_line_head - 1 + MAX_LINES) % MAX_LINES;
    int len = (int)strlen(g_lines[idx]);
    if (len < LINE_LEN - 2) {
        g_lines[idx][len] = c;
        g_lines[idx][len + 1] = '\0';
    }
}

static void redraw(const char* input, int input_len, int cursor_x) {
    window_clear(g_win, 0x0C121B);
    // Draw border
    window_draw_rect(g_win, 0, 0, WIN_W, WIN_H, 0x1A2A3A, 0);
    // Draw connection status
    char status[64];
    snprintf(status, sizeof(status), "Telnet %s", g_connected ? "(connected)" : "");
    window_draw_text(g_win, 6, 4, 0x66AACC, status);
    // Display area
    int vis = DISP_H / 16;
    int start = g_line_head - g_line_count + g_scroll;
    if (start < 0) start = 0;
    for (int i = 0; i < vis && i < g_line_count; i++) {
        int idx = (start + i) % MAX_LINES;
        window_draw_text(g_win, DISP_X, DISP_Y + i * 16, 0xD0E0F0, g_lines[idx]);
    }
    // Input bar
    window_draw_rect(g_win, 2, INPUT_Y, WIN_W - 4, 22, 0x1A2A3A, 1);
    window_draw_rect(g_win, 2, INPUT_Y, WIN_W - 4, 22, 0x334455, 0);
    if (!g_connected) {
        char buf[256];
        snprintf(buf, sizeof(buf), "Host: %s", input);
        window_draw_text(g_win, 6, INPUT_Y + 3, 0xC0D0E0, buf);
    } else {
        window_draw_text(g_win, 6, INPUT_Y + 3, 0x8090A0, "[connected]");
    }
    window_present(g_win);
}

static int tcp_send_all(const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    size_t rem = len;
    while (rem > 0) {
        long n = sys_tcp_send(p, rem);
        if (n <= 0) return -1;
        p += n;
        rem -= (size_t)n;
    }
    return 0;
}

static void tn_respond(uint8_t cmd, uint8_t opt) {
    uint8_t buf[3] = { TN_IAC, (cmd == TN_DO || cmd == TN_DONT) ? TN_WONT : TN_DONT, opt };
    tcp_send_all(buf, 3);
}

static const char* find_host(const char* input, int* out_port) {
    *out_port = 23;
    if (!input || !input[0]) return NULL;
    const char* colon = strchr(input, ':');
    if (colon) {
        *out_port = atoi(colon + 1);
        if (*out_port <= 0) *out_port = 23;
    }
    static char host[256];
    int len = colon ? (int)(colon - input) : (int)strlen(input);
    if (len > 255) len = 255;
    memcpy(host, input, (size_t)len);
    host[len] = '\0';
    return host;
}

void ntux_user_entry(void) {
    const char* arg_host = ntux_arg(1);
    const char* arg_port = ntux_arg(2);

    if (window_init() != 0) sys_exit(1);
    if (window_create(g_win, 100, 80, WIN_W, WIN_H, 0x0C121B, "Telnet") != 0) sys_exit(1);
    window_show(g_win, 1);
    window_focus(g_win);

    char input_buf[256];
    int input_len = 0;
    if (arg_host && arg_host[0]) {
        snprintf(input_buf, sizeof(input_buf), arg_port ? "%s:%s" : "%s", arg_host, arg_port ? arg_port : "");
        input_len = (int)strlen(input_buf);
    }

    enum { DATA, IAC, CMD, SB, SB_DATA, SB_IAC } tn_state = DATA;
    uint8_t tn_cmd = 0;
    int escape = 0;

    push_line("Telnet Client");
    push_line("Type a host:port in the input bar and press Enter to connect.");
    redraw(input_buf, input_len, 0);

    while (!g_quit) {
        if (window_should_close(g_win)) break;

        // --- Connected: handle telnet I/O ---
        if (g_connected) {
            uint8_t in[4096];
            long n = sys_tcp_recv_nb(in, sizeof(in));
            if (n == -2) {
                push_line("[connection closed]");
                g_connected = 0;
                redraw(input_buf, input_len, 0);
            } else if (n > 0) {
                for (long i = 0; i < n; i++) {
                    uint8_t b = in[i];
                    switch (tn_state) {
                    case DATA:
                        if (b == TN_IAC) tn_state = IAC;
                        else push_char((char)b);
                        break;
                    case IAC:
                        switch (b) {
                        case TN_IAC: push_char((char)b); tn_state = DATA; break;
                        case TN_WILL: case TN_WONT: case TN_DO: case TN_DONT: tn_cmd = b; tn_state = CMD; break;
                        case TN_SB: tn_state = SB; break;
                        case TN_SE: tn_state = DATA; break;
                        default: tn_state = DATA; break;
                        }
                        break;
                    case CMD:
                        tn_respond(tn_cmd, b);
                        tn_state = DATA;
                        break;
                    case SB: tn_state = SB_DATA; break;
                    case SB_DATA:
                        if (b == TN_IAC) tn_state = SB_IAC;
                        break;
                    case SB_IAC:
                        if (b == TN_SE) tn_state = DATA;
                        else tn_state = (b == TN_IAC) ? SB_DATA : SB_DATA;
                        break;
                    }
                }
                redraw(input_buf, input_len, 0);
            }

            // Keyboard input while connected
            int sc_map[] = {
                'a',0x1E, 'b',0x30, 'c',0x2E, 'd',0x20, 'e',0x12, 'f',0x21,
                'g',0x22, 'h',0x23, 'i',0x17, 'j',0x24, 'k',0x25, 'l',0x26,
                'm',0x32, 'n',0x31, 'o',0x18, 'p',0x19, 'q',0x10, 'r',0x13,
                's',0x1F, 't',0x14, 'u',0x16, 'v',0x2F, 'w',0x11, 'x',0x2D,
                'y',0x15, 'z',0x2C,
                '0',0x0B, '1',0x02, '2',0x03, '3',0x04, '4',0x05,
                '5',0x06, '6',0x07, '7',0x08, '8',0x09, '9',0x0A,
                '`',0x29, '-',0x0C, '=',0x0D, '[',0x1A, ']',0x1B,
                ';',0x27, '\'',0x28, ',',0x33, '.',0x34, '/',0x35,
                ' ',0x39, '\n',0x1C, '\b',0x0E, '\t',0x0F,
                0
            };
            for (int i = 0; sc_map[i]; i += 2) {
                if (key_edge(sc_map[i + 1])) {
                    char ch = (char)sc_map[i];
                    uint8_t chb = (uint8_t)ch;
                    tcp_send_all(&chb, 1);
                }
            }
            if (key_edge(0x01)) { // Escape
                g_quit = 1;
                break;
            }
        }

        // --- Input handling (both before connect and while connected for escape) ---
        window_input_state_t st;
        if (window_get_input_state(g_win, &st) == 0) {
            // Check mouse click on input area
            if (st.mouse_left && st.mouse_y >= INPUT_Y && st.mouse_y < INPUT_Y + 22) {
                // Click in input bar - just ensure focus
            }
        }

        // Read keystrokes via getchar for text input
        long ch = sys_getchar();
        if (ch > 0) {
            if (g_connected) {
                if (ch == ESC_CHAR) {
                    escape = 1;
                    push_line("[telnet] Ctrl+] pressed. Press q to quit.");
                    redraw(input_buf, input_len, 0);
                    continue;
                }
                if (escape) {
                    if (ch == 'q' || ch == 'Q') {
                        g_quit = 1;
                        break;
                    }
                    escape = 0;
                    continue;
                }
            } else {
                // Input bar editing
                if (ch == '\n' || ch == '\r') {
                    input_buf[input_len] = '\0';
                    if (input_len > 0) {
                        int port;
                        const char* host = find_host(input_buf, &port);
                        if (!host) continue;

                        net_ipv4_address_t ip;
                        snprintf(g_lines[0], LINE_LEN, "Resolving %s ...", host);
                        g_line_count = 1;
                        g_line_head = 1;
                        redraw(input_buf, input_len, 0);

                        if (sys_dns_lookup(host, &ip) != 0) {
                            push_line("DNS lookup failed");
                            continue;
                        }
                        snprintf(input_buf, sizeof(input_buf), "Connecting to %u.%u.%u.%u:%u ...",
                            (unsigned)ip.bytes[0], (unsigned)ip.bytes[1],
                            (unsigned)ip.bytes[2], (unsigned)ip.bytes[3],
                            (unsigned)port);
                        push_line(input_buf);
                        redraw(input_buf, input_len, 0);

                        if (sys_tcp_connect(&ip, (uint16_t)port) != 0) {
                            push_line("Connection failed");
                            continue;
                        }
                        g_connected = 1;
                        push_line("Connected.");
                        input_len = 0;
                        input_buf[0] = '\0';
                        redraw(input_buf, input_len, 0);
                    }
                } else if (ch == '\b' || ch == 127) {
                    if (input_len > 0) input_len--;
                    redraw(input_buf, input_len, 0);
                } else if (ch >= 32 && ch < 127) {
                    if (input_len < 255) {
                        input_buf[input_len++] = (char)ch;
                        input_buf[input_len] = '\0';
                        redraw(input_buf, input_len, 0);
                    }
                }
            }
        }

        sys_wait_ticks(1);
    }

    if (g_connected) sys_tcp_close();
    window_close(g_win);
    sys_exit(0);
}
