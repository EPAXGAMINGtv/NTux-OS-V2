#include "libui.h"
#include <string.h>
#include <stdlib.h>
#include <syscall.h>

#define FONT_W 8
#define FONT_H 8

static uint64_t win_counter = 0x42524F575345525Full;
static int event_queue_head=0,event_queue_tail=0;
static gui_event_t event_queue[128];
static int last_mouse_left=0,last_mouse_right=0;
static int last_win_w=1280,last_win_h=960;

ui_window_t ui_window_create(const char*title,int x,int y,int w,int h){
    (void)title;
    ui_window_t id = win_counter++;
    if(window_init() != 0) return 0;
    if(window_create(id,x,y,w,h,0xFF162230u,title?title:"Window") != 0) return 0;
    last_win_w = w; last_win_h = h;
    return id;
}
void ui_window_set_title(ui_window_t win,const char*title){
    window_set_title(win,title);
}
void ui_window_set_resizable(ui_window_t win,int resizable){
    (void)win; (void)resizable;
}
void ui_set_font(ui_window_t win,const char*path){
    (void)win; (void)path;
}
void ui_draw_rect(ui_window_t win,int x,int y,int w,int h,uint32_t color){
    window_draw_rect(win,x,y,w,h,color,1);
}
void ui_draw_rounded_rect_filled(ui_window_t win,int x,int y,int w,int h,int r,uint32_t color){
    (void)r;
    window_draw_rect(win,x,y,w,h,color,1);
}
void ui_draw_string(ui_window_t win,int x,int y,const char*str,uint32_t color){
    window_draw_text(win,x,y,color,str);
}
void ui_draw_string_scaled(ui_window_t win,int x,int y,const char*str,uint32_t color,float scale){
    (void)scale;
    window_draw_text(win,x,y,color,str);
}
void ui_draw_image(ui_window_t win,int x,int y,int w,int h,const uint32_t*pixels){
    window_draw_image_raw(win,x,y,w,h,4,(const char*)pixels,(uint32_t)(w*h*4));
}
int ui_get_string_width(const char*str){
    if(!str) return 0;
    return (int)strlen(str)*FONT_W;
}
int ui_get_string_width_scaled(const char*str,float scale){
    (void)scale;
    return ui_get_string_width(str);
}
int ui_get_font_height_scaled(float scale){
    (void)scale;
    return FONT_H+4;
}
void ui_mark_dirty(ui_window_t win,int x,int y,int w,int h){
    (void)x; (void)y; (void)w; (void)h;
    window_present(win);
}

int ui_get_event(ui_window_t win,gui_event_t*ev){
    if(!ev) return 0;
    /* Try queue first */
    if(event_queue_head != event_queue_tail){
        *ev = event_queue[event_queue_head];
        event_queue_head = (event_queue_head+1)&127;
        return 1;
    }
    /* Poll window state */
    window_input_state_t st;
    memset(&st,0,sizeof(st));
    window_get_input_state(win,&st);

    /* Resize */
    if(st.win_w != last_win_w || st.win_h != last_win_h){
        last_win_w = st.win_w; last_win_h = st.win_h;
        ev->type = GUI_EVENT_RESIZE;
        ev->arg1 = st.win_w; ev->arg2 = st.win_h;
        return 1;
    }

    /* Close */
    if(st.close_requested){
        ev->type = GUI_EVENT_CLOSE; return 1;
    }

    /* Mouse wheel */
    if(st.mouse_scroll != 0){
        ev->type = GUI_EVENT_MOUSE_WHEEL;
        ev->arg1 = st.mouse_scroll;
        return 1;
    }

    /* Mouse buttons */
    int left_edge = (st.mouse_left && !last_mouse_left);
    int left_release = (!st.mouse_left && last_mouse_left);
    last_mouse_left = st.mouse_left;
    if(left_edge){
        ev->type = GUI_EVENT_CLICK;
        ev->arg1 = st.mouse_x;
        ev->arg2 = st.mouse_y;
        return 1;
    }

    /* Keyboard - collect multiple chars */
    long ch;
    int found=0;
    while((ch=sys_getchar())>0 && !found){
        if(!found){
            ev->type = GUI_EVENT_KEY;
            ev->arg1 = (int)ch;
            ev->arg4 = (uint32_t)ch;
            found = 1;
        }
    }
    /* Drain extra chars from buffer into queue */
    while((ch=sys_getchar())>0){
        int next = (event_queue_tail+1)&127;
        if(next != event_queue_head){
            event_queue[event_queue_tail].type = GUI_EVENT_KEY;
            event_queue[event_queue_tail].arg1 = (int)ch;
            event_queue[event_queue_tail].arg4 = (uint32_t)ch;
            event_queue_tail = next;
        }
    }
    if(found) return 1;

    return 0;
}

/* ── Widget implementations ───────────────────────────────── */
void widget_scrollbar_init(widget_scrollbar_t*sb,int x,int y,int w,int h){
    memset(sb,0,sizeof(*sb));sb->x=x;sb->y=y;sb->w=w;sb->h=h;
}
void widget_scrollbar_update(widget_scrollbar_t*sb,int content_h,int scroll_y){
    sb->content_h=content_h;sb->view_h=sb->h;sb->scroll_y=scroll_y;
}
void widget_scrollbar_draw(widget_context_t*ctx,widget_scrollbar_t*sb){
    if(!ctx||!ctx->user_data)return;
    ui_window_t win=(ui_window_t)(uintptr_t)ctx->user_data;
    int vp_h=sb->h;
    window_draw_scrollbar(win,sb->x,sb->y,sb->w,vp_h,sb->content_h,vp_h,sb->scroll_y,0);
}
int widget_scrollbar_handle_mouse(widget_scrollbar_t*sb,int mx,int my,int is_down,widget_context_t*ctx){
    (void)ctx;
    if(is_down){
        if(mx>=sb->x&&mx<sb->x+sb->w&&my>=sb->y&&my<sb->y+sb->h){
            if(!sb->is_dragging){
                sb->is_dragging=1;
                sb->drag_start_y=my;
                sb->scroll_start=sb->scroll_y;
            }
        }
    }else{
        if(sb->is_dragging){
            sb->is_dragging=0;
            return 1;
        }
    }
    if(sb->is_dragging){
        int dy=my-sb->drag_start_y;
        int max_scroll=sb->content_h-sb->view_h;
        if(max_scroll<0)max_scroll=0;
        sb->scroll_y=sb->scroll_start+(dy*sb->content_h)/sb->h;
        if(sb->scroll_y>max_scroll)sb->scroll_y=max_scroll;
        if(sb->scroll_y<0)sb->scroll_y=0;
        if(sb->on_scroll)sb->on_scroll(ctx->user_data,sb->scroll_y);
        return 1;
    }
    return 0;
}
void widget_textbox_init(widget_textbox_t*tb,int x,int y,int w,int h,const char*text,int max_len){
    memset(tb,0,sizeof(*tb));tb->x=x;tb->y=y;tb->w=w;tb->h=h;
    tb->cursor_pos=0;tb->focused=0;
    if(text){int i=0;while(text[i]&&i<max_len&&i<511){tb->text[i]=text[i];i++;}tb->text[i]=0;}
}
void widget_textbox_draw(widget_context_t*ctx,widget_textbox_t*tb){
    if(!ctx||!ctx->user_data)return;
    ui_window_t win=(ui_window_t)(uintptr_t)ctx->user_data;
    uint32_t bg=0xFF1A2636u,fg=0xFFE6F1FFu,border=0xFF2A3D55u;
    if(ctx->use_light_theme){bg=0xFFFFFFFF;fg=0xFF000000;border=0xFF888888;}
    ui_draw_rect(win,tb->x,tb->y,tb->w,tb->h,bg);
    window_draw_rect(win,tb->x,tb->y,tb->w,tb->h,border,0);
    if(tb->text[0])window_draw_text(win,tb->x+4,tb->y+6,fg,tb->text);
    if(tb->focused){
        int cx=(int)strlen(tb->text)*FONT_W;
        if(cx<tb->w-8)window_draw_rect(win,tb->x+4+cx,tb->y+5,1,tb->h-10,fg,1);
    }
}
int widget_textbox_handle_mouse(widget_context_t*ctx,widget_textbox_t*tb,int mx,int my,int is_click,char*result){
    (void)ctx;(void)result;
    if(is_click&&mx>=tb->x&&mx<tb->x+tb->w&&my>=tb->y&&my<tb->y+tb->h){
        tb->focused=1;
        int cx=(mx-tb->x-4)/FONT_W;
        int len=(int)strlen(tb->text);
        if(cx<0)cx=0;if(cx>len)cx=len;
        tb->cursor_pos=cx;
        return 1;
    }
    return 0;
}
void widget_button_init(widget_button_t*btn,int x,int y,int w,int h,const char*text){
    memset(btn,0,sizeof(*btn));btn->x=x;btn->y=y;btn->w=w;btn->h=h;btn->text=text;
}
void widget_button_draw(widget_context_t*ctx,widget_button_t*btn){
    if(!ctx||!ctx->user_data)return;
    ui_window_t win=(ui_window_t)(uintptr_t)ctx->user_data;
    uint32_t bg=0xFF25364Au,fg=0xFF4BB3F5u;
    if(ctx->use_light_theme){bg=0xFFE0E0E0;fg=0xFF000000;}
    window_draw_rect(win,btn->x,btn->y,btn->w,btn->h,bg,1);
    window_draw_rect(win,btn->x,btn->y,btn->w,btn->h,0xFF2A3D55u,0);
    if(btn->text)window_draw_text(win,btn->x+6,btn->y+6,fg,btn->text);
}
int widget_button_handle_mouse(widget_button_t*btn,int mx,int my,int is_down,int is_click,void*cb){
    (void)is_down;(void)cb;
    if(is_click&&mx>=btn->x&&mx<btn->x+btn->w&&my>=btn->y&&my<btn->y+btn->h)return 1;
    return 0;
}
void widget_checkbox_init(widget_checkbox_t*cb,int x,int y,int w,int h,const char*text,int is_radio){
    memset(cb,0,sizeof(*cb));cb->x=x;cb->y=y;cb->w=w;cb->h=h;cb->is_radio=is_radio;
    if(text){int i=0;while(text[i]&&i<63){cb->text[i]=text[i];i++;}cb->text[i]=0;}
}
void widget_checkbox_draw(widget_context_t*ctx,widget_checkbox_t*cb){
    if(!ctx||!ctx->user_data)return;
    ui_window_t win=(ui_window_t)(uintptr_t)ctx->user_data;
    uint32_t bg=0xFF1A2636u,fg=0xFFE6F1FFu,ac=0xFF4BB3F5u;
    if(ctx->use_light_theme){bg=0xFFFFFFFF;fg=0xFF000000;}
    ui_draw_rect(win,cb->x+2,cb->y+2,cb->w-4,cb->h-4,bg);
    window_draw_rect(win,cb->x,cb->y,cb->w,cb->h,0xFF2A3D55u,0);
    if(cb->checked){
        window_draw_rect(win,cb->x+4,cb->y+4,cb->w-8,cb->h-8,ac,1);
    }
    if(cb->text[0])window_draw_text(win,cb->x+cb->w+4,cb->y+6,fg,cb->text);
}
