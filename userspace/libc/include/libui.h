#ifndef LIBUI_H
#define LIBUI_H

#include <stdint.h>
#include <syscall.h>
#include <window.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Types ────────────────────────────────────────────────── */
typedef window_t ui_window_t;

#define GUI_EVENT_PAINT       1
#define GUI_EVENT_RESIZE      2
#define GUI_EVENT_CLICK       3
#define GUI_EVENT_MOUSE_DOWN  4
#define GUI_EVENT_MOUSE_UP    5
#define GUI_EVENT_MOUSE_MOVE  6
#define GUI_EVENT_KEY         7
#define GUI_EVENT_KEYUP       8
#define GUI_EVENT_MOUSE_WHEEL 9
#define GUI_EVENT_CLOSE       12

typedef struct {
    int type;
    int arg1, arg2, arg3;
    uint32_t arg4;
    uint64_t window;
} gui_event_t;

/* ── Widget types ─────────────────────────────────────────── */
typedef struct {
    void *user_data;
    int use_light_theme;
    void (*draw_rect)(void*,int,int,int,int,uint32_t);
    void (*draw_rounded_rect_filled)(void*,int,int,int,int,int,uint32_t);
    void (*draw_string)(void*,int,int,const char*,uint32_t);
    int  (*measure_string_width)(void*,const char*);
    void (*mark_dirty)(void*,int,int,int,int);
} widget_context_t;

typedef struct {
    int x,y,w,h;
    int scroll_y;
    int content_h,view_h;
    int is_dragging;
    int drag_start_y,scroll_start;
    void (*on_scroll)(void*,int);
} widget_scrollbar_t;

typedef struct {
    int x,y,w,h;
    char text[512];
    int cursor_pos;
    int focused;
} widget_textbox_t;

typedef struct {
    int x,y,w,h;
    const char*text;
} widget_button_t;

typedef struct {
    int x,y,w,h;
    char text[64];
    int checked;
    int is_radio;
} widget_checkbox_t;

/* ── UI functions ─────────────────────────────────────────── */
ui_window_t ui_window_create(const char*title,int x,int y,int w,int h);
void ui_window_set_title(ui_window_t win,const char*title);
void ui_window_set_resizable(ui_window_t win,int resizable);
void ui_set_font(ui_window_t win,const char*path);
void ui_draw_rect(ui_window_t win,int x,int y,int w,int h,uint32_t color);
void ui_draw_rounded_rect_filled(ui_window_t win,int x,int y,int w,int h,int r,uint32_t color);
void ui_draw_string(ui_window_t win,int x,int y,const char*str,uint32_t color);
void ui_draw_string_scaled(ui_window_t win,int x,int y,const char*str,uint32_t color,float scale);
void ui_draw_image(ui_window_t win,int x,int y,int w,int h,const uint32_t*pixels);
int  ui_get_string_width(const char*str);
int  ui_get_string_width_scaled(const char*str,float scale);
int  ui_get_font_height_scaled(float scale);
void ui_mark_dirty(ui_window_t win,int x,int y,int w,int h);
int  ui_get_event(ui_window_t win,gui_event_t*ev);

/* ── Widget drawing ───────────────────────────────────────── */
void widget_scrollbar_init(widget_scrollbar_t*sb,int x,int y,int w,int h);
void widget_scrollbar_update(widget_scrollbar_t*sb,int content_h,int scroll_y);
void widget_scrollbar_draw(widget_context_t*ctx,widget_scrollbar_t*sb);
int  widget_scrollbar_handle_mouse(widget_scrollbar_t*sb,int mx,int my,int is_down,widget_context_t*ctx);
void widget_textbox_init(widget_textbox_t*tb,int x,int y,int w,int h,const char*text,int max_len);
void widget_textbox_draw(widget_context_t*ctx,widget_textbox_t*tb);
int  widget_textbox_handle_mouse(widget_context_t*ctx,widget_textbox_t*tb,int mx,int my,int is_click,char*result);
void widget_button_init(widget_button_t*btn,int x,int y,int w,int h,const char*text);
void widget_button_draw(widget_context_t*ctx,widget_button_t*btn);
int  widget_button_handle_mouse(widget_button_t*btn,int mx,int my,int is_down,int is_click,void*cb);
void widget_checkbox_init(widget_checkbox_t*cb,int x,int y,int w,int h,const char*text,int is_radio);
void widget_checkbox_draw(widget_context_t*ctx,widget_checkbox_t*cb);

#ifdef __cplusplus
}
#endif

#endif