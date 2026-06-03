#include <syscall.h>
#include <window.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_TABS 20
#define URL_MAX 512
#define CONTENT_MAX 65536
#define LINE_CAP 512
#define LINE_MAX 200
#define DOM_ATTR_MAX 32
#define CSS_RULE_MAX 256

typedef struct dom_node {
    int type;
    char tag[32];
    char text[256];
    struct {
        char name[32];
        char value[128];
    } attrs[DOM_ATTR_MAX];
    int attr_count;
    struct dom_node* parent;
    struct dom_node* first_child;
    struct dom_node* last_child;
    struct dom_node* next_sibling;
} dom_node_t;

typedef struct {
    char selector[128];
    char properties[1024];
} css_rule_t;

typedef struct {
    char title[64];
    char url[URL_MAX];
    char content[CONTENT_MAX];
    dom_node_t* dom_root;
    css_rule_t css_rules[CSS_RULE_MAX];
    int css_count;
    char lines[LINE_CAP][LINE_MAX];
    uint32_t line_color[LINE_CAP];
    uint32_t line_bg[LINE_CAP];
    uint32_t line_link[LINE_CAP];
    dom_node_t* line_input[LINE_CAP];
    char line_decoration[LINE_CAP];
    int line_count;
    int is_html;
    int loading;
    int scroll_y;
} browser_tab_t;

static browser_tab_t g_tabs[MAX_TABS];
static int g_tab_count = 1;
static int g_tab_active = 0;
static int g_addr_focus = 0;
static char g_addr_input[URL_MAX] = "";
static uint8_t g_key_last[128];
static dom_node_t* g_focused_input = NULL;
static int g_input_cursor_visible = 0;
static int g_input_cursor_timer = 0;

static int key_edge(int sc) {
    int now = sys_kbd_is_pressed((uint8_t)sc) ? 1 : 0;
    int edge = (now && !g_key_last[sc]) ? 1 : 0;
    g_key_last[sc] = (uint8_t)now;
    return edge;
}

static int text_hit(int mx, int my, int x, int y, int w, int h) {
    return (mx >= x && my >= y && mx < x + w && my < y + h);
}

static int is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static int ci_eq(const char* a, const char* b) {
    while (*a && *b) { if (lower(*a) != lower(*b)) return 0; a++; b++; }
    return *a == *b;
}

static int starts_with_ci(const char* s, const char* pfx) {
    for (size_t i = 0; pfx[i]; i++) if (lower(s[i]) != lower(pfx[i])) return 0;
    return 1;
}

static void trim_trailing(char* s) {
    int len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\r' || s[len-1] == '\n')) s[--len] = '\0';
}

static uint32_t hex_color(const char* s) {
    if (!s || s[0] != '#') return 0;
    unsigned v = 0; int n = 0;
    for (int i = 1; s[i] && n < 6; i++) {
        char c = s[i]; unsigned d;
        if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (unsigned)(c - 'A' + 10);
        else break;
        v = (v << 4) | d; n++;
    }
    return (n >= 6) ? (0xFF000000u | v) : 0;
}

static const char* skip_ws(const char* s) {
    while (s && *s && is_ws(*s)) s++;
    return s;
}

static dom_node_t* dom_alloc(void) {
    dom_node_t* n = (dom_node_t*)malloc(sizeof(dom_node_t));
    if (n) memset(n, 0, sizeof(dom_node_t));
    return n;
}

static void dom_free(dom_node_t* node) {
    if (!node) return;
    dom_node_t* c = node->first_child;
    while (c) { dom_node_t* next = c->next_sibling; dom_free(c); c = next; }
    free(node);
}

static void dom_add_child(dom_node_t* parent, dom_node_t* child) {
    child->parent = parent;
    child->next_sibling = NULL;
    if (parent->last_child) { parent->last_child->next_sibling = child; parent->last_child = child; }
    else { parent->first_child = parent->last_child = child; }
}

static int is_void_element(const char* tag) {
    return ci_eq(tag, "br") || ci_eq(tag, "hr") || ci_eq(tag, "img") ||
           ci_eq(tag, "input") || ci_eq(tag, "meta") || ci_eq(tag, "link");
}

static int parse_attr(const char* t, int tlen, char* name, int nmax, char* value, int vmax) {
    (void)tlen; (void)nmax; (void)vmax;
    const char* p = skip_ws(t);
    int npos = 0, vpos = 0;
    while (*p && !is_ws(*p) && *p != '>' && *p != '=' && npos < nmax - 1) name[npos++] = lower(*p++);
    name[npos] = '\0';
    if (*p != '=') { value[0] = '\0'; return (int)(p - t); }
    p++;
    if (*p == '"' || *p == '\'') { char q = *p++; while (*p && *p != q && vpos < vmax - 1) value[vpos++] = *p++; if (*p == q) p++; }
    else { while (*p && !is_ws(*p) && *p != '>' && vpos < vmax - 1) value[vpos++] = *p++; }
    value[vpos] = '\0';
    return (int)(p - t);
}

static dom_node_t* html_parse(const char* html) {
    if (!html || !html[0]) return NULL;
    dom_node_t* root = dom_alloc();
    if (!root) return NULL;
    root->type = 1; root->tag[0] = '_'; root->tag[1] = 'r'; root->tag[2] = 'o'; root->tag[3] = 'o'; root->tag[4] = 't'; root->tag[5] = '\0';
    dom_node_t* stack[256];
    int sp = 0;
    stack[sp] = root;
    char text_buf[256];
    int tp = 0;
    int in_style = 0, in_script = 0;

    for (int i = 0; html[i]; i++) {
        char c = html[i];
        if (c == '<') {
            if (tp > 0 && !in_style && !in_script) {
                text_buf[tp] = '\0'; trim_trailing(text_buf);
                if (text_buf[0]) {
                    dom_node_t* tn = dom_alloc();
                    if (tn) { tn->type = 0; strncpy(tn->text, text_buf, sizeof(tn->text) - 1); dom_add_child(stack[sp], tn); }
                }
                tp = 0;
            }
            const char* tag_start = html + i + 1;
            const char* tag_end = strchr(tag_start, '>');
            if (!tag_end) break;
            int tag_len = (int)(tag_end - tag_start);
            if (tag_len > 255) tag_len = 255;
            char tag_buf[256];
            memcpy(tag_buf, tag_start, (size_t)tag_len);
            tag_buf[tag_len] = '\0';
            const char* t = skip_ws(tag_buf);
            int closing = (*t == '/');
            if (closing) t++;
            char tname[32]; int tnp = 0;
            while (*t && !is_ws(*t) && *t != '>' && tnp < 31) tname[tnp++] = lower(*t++);
            tname[tnp] = '\0';

            if (closing) {
                if (ci_eq(tname, "style")) in_style = 0;
                if (ci_eq(tname, "script")) in_script = 0;
                for (int j = sp; j >= 0; j--) {
                    if (stack[j] && ci_eq(stack[j]->tag, tname)) { sp = j - 1; if (sp < 0) sp = 0; break; }
                }
                i = (size_t)(tag_end - html);
                continue;
            }

            if (ci_eq(tname, "style")) { in_style = 1; i = (size_t)(tag_end - html); continue; }
            if (ci_eq(tname, "script")) { in_script = 1; i = (size_t)(tag_end - html); continue; }

            dom_node_t* en = dom_alloc();
            if (!en) { i = (size_t)(tag_end - html); continue; }
            en->type = 1;
            strncpy(en->tag, tname, sizeof(en->tag) - 1);

            const char* ap = t;
            while (*ap && tnp > 0) { ap++; tnp--; }
            while (*ap && is_ws(*ap)) ap++;
            while (*ap && *ap != '>' && en->attr_count < DOM_ATTR_MAX) {
                char an[32], av[128];
                int consumed = parse_attr(ap, (int)(tag_end - ap), an, 32, av, 128);
                if (consumed <= 0) break;
                if (an[0]) {
                    strncpy(en->attrs[en->attr_count].name, an, sizeof(en->attrs[en->attr_count].name) - 1);
                    strncpy(en->attrs[en->attr_count].value, av, sizeof(en->attrs[en->attr_count].value) - 1);
                    en->attr_count++;
                }
                ap += consumed;
                while (*ap && is_ws(*ap)) ap++;
            }

            dom_add_child(stack[sp], en);
            if (!is_void_element(tname) && !ci_eq(tname, "style") && !ci_eq(tname, "script")) {
                if (sp < 255) stack[++sp] = en;
            }
            i = (size_t)(tag_end - html);
        } else {
            if (in_style || in_script) continue;
            if (tp < 255) text_buf[tp++] = c;
        }
    }
    if (tp > 0 && !in_style && !in_script) {
        text_buf[tp] = '\0'; trim_trailing(text_buf);
        if (text_buf[0]) {
            dom_node_t* tn = dom_alloc();
            if (tn) { tn->type = 0; strncpy(tn->text, text_buf, sizeof(tn->text) - 1); dom_add_child(stack[sp], tn); }
        }
    }
    return root;
}

static const char* find_attr(dom_node_t* node, const char* name) {
    for (int i = 0; i < node->attr_count; i++)
        if (ci_eq(node->attrs[i].name, name)) return node->attrs[i].value;
    return NULL;
}

static void css_parse(css_rule_t* rules, int* count, const char* css) {
    if (!css) return;
    const char* p = css;
    while (*p && *count < CSS_RULE_MAX) {
        while (*p && (is_ws(*p) || *p == ',')) p++;
        if (!*p) break;
        char sel[128]; int sp = 0;
        int brace = 0;
        while (*p && sp < 127) {
            if (*p == '{') { brace = 1; break; }
            sel[sp++] = *p++;
        }
        sel[sp] = '\0';
        trim_trailing(sel);
        if (!brace) break;
        p++;
        char props[1024]; int pp = 0;
        while (*p && *p != '}' && pp < 1023) props[pp++] = *p++;
        props[pp] = '\0';
        if (*p == '}') p++;
        if (sel[0]) {
            strncpy(rules[*count].selector, sel, sizeof(rules[*count].selector) - 1);
            strncpy(rules[*count].properties, props, sizeof(rules[*count].properties) - 1);
            (*count)++;
        }
    }
}

static void collect_style_text(dom_node_t* node, char* out, int max) {
    if (!node) return;
    if (node->type == 0) {
        int olen = strlen(out);
        int slen = strlen(node->text);
        if (olen + slen < max - 1) { memcpy(out + olen, node->text, (size_t)slen); out[olen + slen] = '\0'; }
        return;
    }
    dom_node_t* c = node->first_child;
    while (c) { collect_style_text(c, out, max); c = c->next_sibling; }
}

static const char* css_val(const char* props, const char* prop) {
    if (!props) return NULL;
    static char val_buf[128];
    const char* p = props;
    while (*p) {
        while (*p && is_ws(*p)) p++;
        if (!*p) break;
        const char* pstart = p;
        while (*p && *p != ':') p++;
        if (*p != ':') continue;
        int plen = (int)(p - pstart);
        if (plen == (int)strlen(prop)) {
            int match = 1;
            for (int i = 0; i < plen; i++) if (lower(pstart[i]) != lower(prop[i])) { match = 0; break; }
            if (match) {
                p++;
                while (*p && is_ws(*p)) p++;
                int vp = 0;
                while (*p && *p != ';' && vp < 126) val_buf[vp++] = *p++;
                val_buf[vp] = '\0';
                trim_trailing(val_buf);
                return val_buf;
            }
        }
        while (*p && *p != ';') p++;
        if (*p == ';') p++;
    }
    return NULL;
}

static int parse_px(const char* s) {
    if (!s) return 0;
    int v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return v;
}

static int css_matches(dom_node_t* node, const char* selector) {
    if (!node || !selector) return 0;
    const char* s = skip_ws(selector);
    if (!*s) return 0;
    if (s[0] == '*') return 1;
    if (s[0] == '.') {
        const char* cls = find_attr(node, "class");
        if (!cls) return 0;
        s++;
        while (*cls) {
            while (*cls && is_ws(*cls)) cls++;
            if (!*cls) break;
            int match = 1;
            for (int i = 0; s[i] && !is_ws(s[i]); i++) if (lower(cls[i]) != lower(s[i])) { match = 0; break; }
            if (match) return 1;
            while (*cls && !is_ws(*cls)) cls++;
        }
        return 0;
    }
    if (s[0] == '#') {
        const char* id = find_attr(node, "id");
        if (!id) return 0;
        return ci_eq(id, s + 1);
    }
    return starts_with_ci(node->tag, s);
}

typedef struct {
    uint32_t color;
    uint32_t bg;
    int bold;
    int italic;
    int underline;
    int uppercase;
    int font_size;
    const char* align;
    const char* display;
    int margin_top;
    int margin_bottom;
    int margin_left;
    int padding_left;
    int padding_right;
    int border_left;
    int width;
} css_style_t;

static int px_to_lines(int px) {
    if (px <= 0) return 0;
    return (px + 5) / 10;
}

static void apply_css_props(css_style_t* out, const char* props) {
    if (!props) return;
    const char* v;
    if ((v = css_val(props, "color")) != NULL) { uint32_t c = hex_color(v); if (c) out->color = c; }
    if ((v = css_val(props, "background-color")) != NULL) { uint32_t c = hex_color(v); if (c) out->bg = c; }
    if ((v = css_val(props, "background")) != NULL) { uint32_t c = hex_color(v); if (c) out->bg = c; }
    if (css_val(props, "font-weight")) out->bold = 1;
    if (css_val(props, "font-style")) out->italic = 1;
    if (css_val(props, "text-decoration")) out->underline = 1;
    if (css_val(props, "text-transform")) out->uppercase = 1;
    if ((v = css_val(props, "font-size")) != NULL) { int sz = parse_px(v); if (sz > 0) out->font_size = sz; }
    if ((v = css_val(props, "text-align")) != NULL) out->align = v;
    if ((v = css_val(props, "display")) != NULL) out->display = v;
    if ((v = css_val(props, "margin")) != NULL) { int m = px_to_lines(parse_px(v)); out->margin_top = m; out->margin_bottom = m; out->margin_left = m; }
    if ((v = css_val(props, "margin-top")) != NULL) out->margin_top = px_to_lines(parse_px(v));
    if ((v = css_val(props, "margin-bottom")) != NULL) out->margin_bottom = px_to_lines(parse_px(v));
    if ((v = css_val(props, "margin-left")) != NULL) out->margin_left = px_to_lines(parse_px(v));
    if ((v = css_val(props, "margin-right")) != NULL) { int mr = px_to_lines(parse_px(v)); out->margin_left = mr > out->margin_left ? mr : out->margin_left; }
    if ((v = css_val(props, "padding")) != NULL) { int p = px_to_lines(parse_px(v)); out->padding_left = p; }
    if ((v = css_val(props, "padding-left")) != NULL) out->padding_left = px_to_lines(parse_px(v));
    if ((v = css_val(props, "padding-right")) != NULL) out->padding_right = px_to_lines(parse_px(v));
    if ((v = css_val(props, "padding-top")) != NULL) { int pt = px_to_lines(parse_px(v)); if (pt > out->margin_top) out->margin_top = pt; }
    if ((v = css_val(props, "padding-bottom")) != NULL) { int pb = px_to_lines(parse_px(v)); if (pb > out->margin_bottom) out->margin_bottom = pb; }
    if ((v = css_val(props, "border-left")) != NULL || (v = css_val(props, "border")) != NULL) out->border_left = 1;
    if ((v = css_val(props, "width")) != NULL) out->width = parse_px(v) / 6;
}

static void resolve_css(dom_node_t* node, css_rule_t* rules, int count, css_style_t* out, css_style_t* inherit) {
    if (!node) return;
    if (inherit) {
        *out = *inherit;
    } else {
        memset(out, 0, sizeof(css_style_t));
        out->color = 0xFFE6F1FF;
        out->font_size = 10;
        out->margin_left = 0;
    }
    // Apply rules in order: later rules override earlier ones (CSS spec)
    for (int i = 0; i < count; i++) {
        if (css_matches(node, rules[i].selector)) {
            apply_css_props(out, rules[i].properties);
        }
    }
    const char* style = find_attr(node, "style");
    if (style) {
        apply_css_props(out, style);
    }
}

static void inject_default_css(css_rule_t* rules, int* count) {
    const char* defaults[] = {
        "body { margin-top: 5px; }",
        "h1 { color: #4BB3F5; font-weight: bold; font-size: 18px; text-align: center; margin-top: 10px; margin-bottom: 10px; }",
        "h2 { color: #4BB3F5; font-weight: bold; font-size: 14px; margin-top: 8px; margin-bottom: 6px; }",
        "h3 { color: #4BB3F5; font-weight: bold; margin-top: 6px; margin-bottom: 4px; }",
        "h4, h5, h6 { color: #4BB3F5; font-weight: bold; }",
        "a { color: #4BB3F5; }",
        "p { margin-top: 4px; margin-bottom: 4px; }",
        "b, strong { font-weight: bold; }",
        "i, em { font-style: italic; }",
        "section, .wrapper { display: block; }",
        "footer { display: block; margin-top: 10px; }",
        ".container { margin-left: 15px; margin-right: 15px; }",
        ".major { text-align: center; }",
        "#banner { text-align: center; padding-top: 20px; padding-bottom: 20px; background-color: #1E2A3A; }",
        "#banner h2 { font-size: 20px; text-transform: uppercase; color: #FFFFFF; }",
        ".wrapper.style1 { background-color: #111C26; padding-top: 10px; padding-bottom: 10px; }",
        ".wrapper.style2 { background-color: #162230; padding-top: 10px; padding-bottom: 10px; }",
        ".wrapper.style3 { background-color: #1B2A3A; padding-top: 10px; padding-bottom: 10px; }",
        "div.style1 { background-color: #111C26; padding-top: 10px; padding-bottom: 10px; }",
        "div.style2 { background-color: #162230; padding-top: 10px; padding-bottom: 10px; }",
        "div.style3 { background-color: #1B2A3A; padding-top: 10px; padding-bottom: 10px; }",
        ".copyright { text-align: center; }",
        ".box { margin-left: 15px; margin-right: 15px; padding-left: 10px; padding-right: 10px; }",
        "ul { display: block; margin-top: 6px; margin-bottom: 6px; }",
        "li { display: block; margin-left: 20px; }",
        "header { display: block; }",
        "#contact { border-left: 1px; }",
        "#footer { text-align: center; }",
        ".labeled-icons li { margin-top: 6px; margin-bottom: 6px; }",
    };
    for (int i = 0; i < (int)(sizeof(defaults)/sizeof(defaults[0])) && *count < CSS_RULE_MAX; i++) {
        css_parse(rules, count, defaults[i]);
    }
}

static void render_node(dom_node_t* node, css_rule_t* rules, int rule_count, css_style_t* inherit_style,
                         char lines[LINE_CAP][LINE_MAX], uint32_t* lcolor, uint32_t* lbg, uint32_t* llink,
                         dom_node_t** linput, char* ldec, int* lcount,
                         int* lp, char* linebuf, int ltabs) {
    if (!node || *lcount >= LINE_CAP) return;
    css_style_t style;
    resolve_css(node, rules, rule_count, &style, inherit_style);

    if (node->type == 0) {
        const char* txt = node->text;
        while (*txt) {
            while (*txt && is_ws(*txt) && is_ws(*(txt+1))) txt++;
            if (!*txt) break;
            if (*lp >= LINE_MAX - 1 && *lcount < LINE_CAP) {
                linebuf[*lp] = '\0';
                strncpy(lines[*lcount], linebuf, LINE_MAX - 1);
                lcolor[*lcount] = style.color;
                lbg[*lcount] = style.bg;
                ldec[*lcount] = style.underline ? 'u' : 0;
                (*lcount)++;
                *lp = 0;
            }
            char c = *txt++;
            if (c == '\n' || c == '\r') {
                if (*lp > 0) {
                    linebuf[*lp] = '\0';
                    strncpy(lines[*lcount], linebuf, LINE_MAX - 1);
                    lcolor[*lcount] = style.color;
                    lbg[*lcount] = style.bg;
                    ldec[*lcount] = style.underline ? 'u' : 0;
                    (*lcount)++;
                    *lp = 0;
                }
            } else {
                if (style.width > 0 && *lp >= style.width) {
                    linebuf[*lp] = '\0';
                    strncpy(lines[*lcount], linebuf, LINE_MAX - 1);
                    lcolor[*lcount] = style.color;
                    lbg[*lcount] = style.bg;
                    ldec[*lcount] = style.underline ? 'u' : 0;
                    (*lcount)++;
                    *lp = 0;
                }
                if (style.uppercase && c >= 'a' && c <= 'z') c -= 32;
                linebuf[(*lp)++] = c;
            }
        }
        return;
    }

    uint32_t link_id = 0;
    if (ci_eq(node->tag, "a") && *lcount < LINE_CAP) {
        link_id = (uint32_t)(uintptr_t)node;
        style.color = 0xFF4BB3F5u;
        style.underline = 1;
    }
    if (ci_eq(node->tag, "b") || ci_eq(node->tag, "strong")) style.bold = 1;
    if (ci_eq(node->tag, "i") || ci_eq(node->tag, "em")) style.italic = 1;
    if (ci_eq(node->tag, "h1") || ci_eq(node->tag, "h2") || ci_eq(node->tag, "h3")) { style.color = 0xFF4BB3F5u; style.bold = 1; style.font_size = 14; }

    int is_block = 0;
    if (style.display && ci_eq(style.display, "block")) is_block = 1;
    else if (style.display && ci_eq(style.display, "inline")) is_block = 0;
    else is_block = ci_eq(node->tag, "div") || ci_eq(node->tag, "p") || ci_eq(node->tag, "h1") || ci_eq(node->tag, "h2") ||
                      ci_eq(node->tag, "h3") || ci_eq(node->tag, "h4") || ci_eq(node->tag, "h5") || ci_eq(node->tag, "h6") ||
                      ci_eq(node->tag, "form") || ci_eq(node->tag, "table") || ci_eq(node->tag, "tr") ||
                      ci_eq(node->tag, "ul") || ci_eq(node->tag, "ol") || ci_eq(node->tag, "li") ||
                      ci_eq(node->tag, "br") || ci_eq(node->tag, "hr") || ci_eq(node->tag, "blockquote") ||
                      ci_eq(node->tag, "center") || ci_eq(node->tag, "body") || ci_eq(node->tag, "head");

    if (ci_eq(node->tag, "br")) {
        if (*lp > 0 || *lcount > 0) {
            linebuf[*lp] = '\0';
            if (*lp > 0) { strncpy(lines[*lcount], linebuf, LINE_MAX - 1); lcolor[*lcount] = style.color; lbg[*lcount] = style.bg; llink[*lcount] = link_id; ldec[*lcount] = style.underline ? 'u' : 0; (*lcount)++; *lp = 0; }
        }
        if (*lcount < LINE_CAP) { lines[*lcount][0] = '\0'; lcolor[*lcount] = style.color; lbg[*lcount] = style.bg; ldec[*lcount] = style.underline ? 'u' : 0; (*lcount)++; }
        return;
    }

    if (ci_eq(node->tag, "hr")) {
        if (*lp > 0 && *lcount < LINE_CAP) {
            linebuf[*lp] = '\0'; strncpy(lines[*lcount], linebuf, LINE_MAX - 1); lcolor[*lcount] = style.color; lbg[*lcount] = style.bg; llink[*lcount] = link_id; (*lcount)++; *lp = 0;
        }
        if (*lcount < LINE_CAP) {
            int w = style.width > 0 && style.width < LINE_MAX - 2 ? style.width : LINE_MAX - 2;
            memset(lines[*lcount], '-', w);
            lines[*lcount][w] = '\0';
            lcolor[*lcount] = style.color; lbg[*lcount] = style.bg; (*lcount)++;
        }
        return;
    }

    if (ci_eq(node->tag, "img")) {
        const char* alt = find_attr(node, "alt");
        const char* src = find_attr(node, "src");
        char img_tag[128];
        img_tag[0] = '\0';
        if (src) { img_tag[0] = '['; int ip = 1; const char* sp = src; while (*sp && ip < 30) img_tag[ip++] = *sp++; img_tag[ip++] = ']'; img_tag[ip] = '\0'; }
        else if (alt) { img_tag[0] = '['; int ip = 1; const char* ap = alt; while (*ap && ip < 30) img_tag[ip++] = *ap++; img_tag[ip++] = ']'; img_tag[ip] = '\0'; }
        else strcpy(img_tag, "[img]");
        if (is_block) {
            if (*lp > 0 && *lcount < LINE_CAP) { linebuf[*lp] = '\0'; strncpy(lines[*lcount], linebuf, LINE_MAX - 1); lcolor[*lcount] = style.color; lbg[*lcount] = style.bg; (*lcount)++; *lp = 0; }
            if (*lcount < LINE_CAP) { strncpy(lines[*lcount], img_tag, LINE_MAX - 1); lcolor[*lcount] = style.color; lbg[*lcount] = style.bg; (*lcount)++; }
        } else {
            int ilen = strlen(img_tag);
            for (int ii = 0; ii < ilen && *lp < LINE_MAX - 1; ii++) linebuf[(*lp)++] = img_tag[ii];
        }
        return;
    }

    if (ci_eq(node->tag, "input")) {
        const char* itype = find_attr(node, "type");
        const char* iv = find_attr(node, "value");
        char input_tag[80];
        if (itype && ci_eq(itype, "submit")) {
            strcpy(input_tag, "[");
            strcat(input_tag, iv ? iv : "Submit");
            strcat(input_tag, "]");
        } else if (itype && ci_eq(itype, "button")) {
            strcpy(input_tag, "[");
            strcat(input_tag, iv ? iv : "Button");
            strcat(input_tag, "]");
        } else if (itype && ci_eq(itype, "checkbox")) {
            strcpy(input_tag, "[X]");
        } else if (itype && ci_eq(itype, "radio")) {
            strcpy(input_tag, "( )");
        } else {
            input_tag[0] = '[';
            const char* inp_val = find_attr(node, "value");
            if (inp_val) {
                int ip = 1;
                const char* hp = inp_val;
                while (*hp && ip < 60) input_tag[ip++] = *hp++;
                input_tag[ip++] = ']';
                input_tag[ip] = '\0';
            } else {
                const char* hp = find_attr(node, "placeholder");
                int ip = 1;
                if (hp) { while (*hp && ip < 30) input_tag[ip++] = *hp++; }
                else input_tag[ip++] = '_';
                input_tag[ip++] = ']';
                input_tag[ip] = '\0';
            }
        }
        int ilen = strlen(input_tag);
        int input_line = *lcount;
        for (int ii = 0; ii < ilen && *lp < LINE_MAX - 1; ii++) linebuf[(*lp)++] = input_tag[ii];
        // If this is a text input, track which line the input is on
        if (!itype || (!ci_eq(itype, "submit") && !ci_eq(itype, "button") && !ci_eq(itype, "checkbox") && !ci_eq(itype, "radio"))) {
            if (input_line < LINE_CAP && *lp <= ilen) {
                linput[input_line] = node;
            } else if (*lcount < LINE_CAP) {
                linput[*lcount] = node;
            }
        }
        return;
    }

    if (ci_eq(node->tag, "button")) {
        const char* bt = "[Button]";
        while (*bt && *lp < LINE_MAX - 1) linebuf[(*lp)++] = *bt++;
        return;
    }

    if (ci_eq(node->tag, "center")) {
        style.align = "center";
        if (*lp > 0 && *lcount < LINE_CAP) { linebuf[*lp] = '\0'; strncpy(lines[*lcount], linebuf, LINE_MAX - 1); lcolor[*lcount] = style.color; lbg[*lcount] = style.bg; llink[*lcount] = link_id; ldec[*lcount] = style.underline ? 'u' : 0; (*lcount)++; *lp = 0; }
    }

    // Block start: flush pending text
    if (is_block && *lp > 0 && *lcount < LINE_CAP) {
        linebuf[*lp] = '\0'; strncpy(lines[*lcount], linebuf, LINE_MAX - 1); lcolor[*lcount] = style.color; lbg[*lcount] = style.bg; llink[*lcount] = link_id; ldec[*lcount] = style.underline ? 'u' : 0; (*lcount)++; *lp = 0;
    }

    // Apply margin-top
    int was_empty = (*lp == 0);
    for (int mi = 0; mi < style.margin_top && *lcount < LINE_CAP; mi++) {
        if (!was_empty || mi > 0 || *lcount > 0) {
            lines[*lcount][0] = '\0'; lcolor[*lcount] = style.color; lbg[*lcount] = style.bg; (*lcount)++;
        }
    }

    // Render children - track start line for alignment
    int child_start = *lcount;
    dom_node_t* c = node->first_child;
    while (c) {
        render_node(c, rules, rule_count, &style, lines, lcolor, lbg, llink, linput, ldec, lcount, lp, linebuf, ltabs);
        c = c->next_sibling;
    }

    if (ci_eq(node->tag, "li") && *lp > 0 && *lcount < LINE_CAP) {
        linebuf[*lp] = '\0'; strncpy(lines[*lcount], linebuf, LINE_MAX - 1); lcolor[*lcount] = style.color; lbg[*lcount] = style.bg; llink[*lcount] = link_id; ldec[*lcount] = style.underline ? 'u' : 0; (*lcount)++; *lp = 0;
    }

    // Block end: blank line
    if (is_block && !ci_eq(node->tag, "br") && !ci_eq(node->tag, "hr") && !ci_eq(node->tag, "img")) {
        if (*lcount < LINE_CAP) { lines[*lcount][0] = '\0'; lcolor[*lcount] = style.color; lbg[*lcount] = style.bg; (*lcount)++; }
    }

    // Apply margin-bottom
    for (int mi = 0; mi < style.margin_bottom && *lcount < LINE_CAP; mi++) {
        lines[*lcount][0] = '\0'; lcolor[*lcount] = style.color; lbg[*lcount] = style.bg; (*lcount)++;
    }

    // Apply text alignment only to lines added by this node
    if (style.align && ci_eq(style.align, "center")) {
        int max_len = 120;
        for (int li = child_start; li < *lcount; li++) {
            int llen = strlen(lines[li]);
            if (llen > 0 && llen < max_len) {
                int pad = (max_len - llen) / 2;
                if (pad > 0 && pad + llen < LINE_MAX - 1) {
                    memmove(lines[li] + pad, lines[li], llen + 1);
                    for (int pi = 0; pi < pad; pi++) lines[li][pi] = ' ';
                }
            }
        }
    }
}

static int html_render(browser_tab_t* tab) {
    if (!tab) return 0;
    tab->line_count = 0;
    tab->is_html = 0;
    tab->scroll_y = 0;

    if (tab->dom_root) { dom_free(tab->dom_root); tab->dom_root = NULL; }
    tab->css_count = 0;

    if (!tab->content[0]) return 0;
    if (!strstr(tab->content, "<html") && !strstr(tab->content, "<!DOCTYPE") && !strstr(tab->content, "<body") && !strstr(tab->content, "<div")) {
        tab->is_html = 0;
        return 0;
    }
    tab->is_html = 1;

    tab->dom_root = html_parse(tab->content);
    if (!tab->dom_root) return 0;

    // Inject default CSS first (lower priority), then page CSS overrides
    inject_default_css(tab->css_rules, &tab->css_count);

    // Extract CSS from <style> tags (higher priority, overrides defaults)
    {
        dom_node_t* stack[256];
        int sp = 0;
        stack[0] = tab->dom_root;
        while (sp >= 0) {
            dom_node_t* cur = stack[sp--];
            if (cur->type == 1 && ci_eq(cur->tag, "style")) {
                char style_text[4096] = "";
                collect_style_text(cur, style_text, 4095);
                css_parse(tab->css_rules, &tab->css_count, style_text);
            }
            dom_node_t* cc = cur->first_child;
            while (cc) {
                stack[++sp] = cc;
                cc = cc->next_sibling;
            }
        }
    }

    int lp = 0;
    char linebuf[LINE_MAX];
    css_style_t def_style;
    memset(&def_style, 0, sizeof(def_style));
    def_style.color = 0xFFE6F1FFu;
    def_style.font_size = 10;
    memset(tab->line_link, 0, sizeof(tab->line_link));
    memset(tab->line_input, 0, sizeof(tab->line_input));
    memset(tab->line_decoration, 0, sizeof(tab->line_decoration));

    render_node(tab->dom_root, tab->css_rules, tab->css_count, &def_style,
                tab->lines, tab->line_color, tab->line_bg, tab->line_link,
                tab->line_input, tab->line_decoration,
                &tab->line_count, &lp, linebuf, 0);

    if (lp > 0 && tab->line_count < LINE_CAP) {
        linebuf[lp] = '\0';
        strncpy(tab->lines[tab->line_count], linebuf, LINE_MAX - 1);
        tab->line_color[tab->line_count] = def_style.color;
        tab->line_bg[tab->line_count] = def_style.bg;
        tab->line_count++;
    }

    if (tab->line_count == 0 && tab->line_count < LINE_CAP) {
        strncpy(tab->lines[0], "(empty document)", LINE_MAX - 1);
        tab->line_color[0] = def_style.color;
        tab->line_count = 1;
    }
    return tab->line_count;
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
        long rc = sys_net_http_get(url, result, sizeof(result));
        if (rc > 0 && result[0]) {
            tab_set_title(tab, url);
            tab_set_content(tab, result);
        } else {
            const char* errmsg = "Failed to load page.\n\nCheck URL or try again.\n";
            tab_set_title(tab, "Error");
            tab_set_content(tab, errmsg);
        }
    }
    tab->loading = 0;
}

static void browser_init_tabs(void) {
    memset(g_tabs, 0, sizeof(g_tabs));
    tab_set_title(&g_tabs[0], "New Tab");
    tab_set_content(&g_tabs[0], "NTux Browser\n\nEnter a URL to begin.\nSupports: HTML, CSS, forms, images, links.");
    g_tabs[0].url[0] = '\0';
}

static void draw_ui(window_t win, const window_input_state_t* st, int hover_tab, int hover_btn, int hover_addr, int view_h) {
    (void)st;
    uint32_t bg = 0xFF0C131Au;
    uint32_t bar = 0xFF162230u;
    uint32_t accent = 0xFF4BB3F5u;
    uint32_t text = 0xFFE6F1FFu;
    uint32_t dim = 0xFF6B8DA8u;
    uint32_t active_tab_bg = 0xFF0C131Au;
    uint32_t inactive_tab_bg = 0xFF1B2A3Au;
    uint32_t addr_bg = 0xFF0C131Au;

    window_clear(win, bg);

    // Title bar background
    window_draw_rect(win, 0, 0, 820, 34, bar, 1);

    int x = 10;
    int y = 6;
    int tab_h = 22;
    for (int i = 0; i < g_tab_count; ++i) {
        int w = 120;
        uint32_t tab_bg = (i == g_tab_active) ? active_tab_bg : inactive_tab_bg;
        uint32_t t = (i == g_tab_active) ? accent : dim;
        if (hover_tab == i && i != g_tab_active) tab_bg = 0xFF25364Au;
        window_draw_rect(win, x, y, w, tab_h, tab_bg, 1);
        // Active tab gets top accent line
        if (i == g_tab_active) window_draw_rect(win, x, y, w, 2, accent, 1);
        window_draw_text(win, x + 8, y + 6, t, g_tabs[i].title[0] ? g_tabs[i].title : "Tab");
        x += w + 6;
    }

    int btn_x = x + 4;
    window_draw_button(win, btn_x, y, 26, tab_h, "+", WINDOW_BUTTON_SECONDARY);
    if (hover_btn) window_draw_rect(win, btn_x, y, 26, tab_h, 0x5522AAFFu, 0);

    int addr_y = y + tab_h + 8;
    int addr_x = 10;
    int addr_w = 780;
    int addr_h = 22;
    uint32_t addr_c = g_addr_focus ? 0xFF162230u : addr_bg;
    window_draw_rect(win, addr_x, addr_y, addr_w, addr_h, addr_c, 1);
    window_draw_rect(win, addr_x, addr_y, addr_w, addr_h, dim, 0);
    if (g_addr_focus || g_addr_input[0]) {
        window_draw_text(win, addr_x + 8, addr_y + 6, text, g_addr_input);
    } else {
        window_draw_text(win, addr_x + 8, addr_y + 6, dim, "Enter URL or search...");
    }

    int view_x = 10;
    int view_y = addr_y + addr_h + 8;
    int view_w = 780;

    // Fill view with page background so section colors contrast properly
    window_draw_rect(win, view_x, view_y, view_w, view_h, 0xFF0C131Au, 1);
    window_draw_rect(win, view_x, view_y, view_w, view_h, dim, 0);

    const browser_tab_t* tab = &g_tabs[g_tab_active];
    int col_y = view_y + 8;
    int line_height = 11;
    int visible_lines = view_h / line_height;

    if (tab->loading) {
        window_draw_text(win, view_x + view_w - 80, view_y + view_h - 16, accent, "Loading...");
    }

    if (tab->line_count > 0) {
        int start = tab->scroll_y;
        if (start >= tab->line_count) start = tab->line_count - 1;
        if (start < 0) start = 0;
        int drawn = 0;
        for (int i = start; i < tab->line_count && drawn < visible_lines; ++i) {
            int py = col_y + drawn * line_height;
            if (py > view_y + view_h - 14) break;
            uint32_t lc = tab->line_color[i] ? tab->line_color[i] : text;
            uint32_t lbg = tab->line_bg[i];
            // Highlight focused input line
            if (g_focused_input && tab->line_input[i] == g_focused_input) {
                lbg = 0xFF1F2B3Bu;
            }
            // Show underlined links
            int is_underline = tab->line_decoration[i] == 'u';
            // Fill background full viewport width for proper section styling
            if (lbg) { window_draw_rect(win, view_x, py - 1, view_w, line_height, lbg, 0); }
            window_draw_text(win, view_x + 8, py, lc, tab->lines[i]);
            if (is_underline) {
                int tlen = strlen(tab->lines[i]);
                window_draw_rect(win, view_x + 8, py + line_height - 1, tlen * 6, 1, lc, 0);
            }
            // Draw cursor on focused input line
            if (g_focused_input && tab->line_input[i] == g_focused_input && g_input_cursor_visible) {
                int tlen = strlen(tab->lines[i]);
                window_draw_rect(win, view_x + 8 + tlen * 6, py, 3, line_height, 0xFFE6F1FFu, 0);
            }
            drawn++;
        }
    } else if (!tab->loading) {
        window_draw_text(win, view_x + 8, col_y, text, "(empty)");
    }

    if (hover_addr) window_draw_rect(win, addr_x, addr_y, addr_w, addr_h, 0x33FFFFFFu, 0);

    // Scrollbar
    if (tab->line_count > visible_lines) {
        int sb_x = view_x + view_w - 8;
        int sb_w = 6;
        int sb_h = view_h;
        int thumb_h = (visible_lines * sb_h) / tab->line_count;
        if (thumb_h < 10) thumb_h = 10;
        int thumb_y = view_y + (tab->scroll_y * (sb_h - thumb_h)) / (tab->line_count - visible_lines);
        if (thumb_y < view_y) thumb_y = view_y;
        if (thumb_y + thumb_h > view_y + view_h) thumb_y = view_y + view_h - thumb_h;
        window_draw_rect(win, sb_x, view_y, sb_w, sb_h, 0xFF0A111Au, 1);
        window_draw_rect(win, sb_x + 1, thumb_y, sb_w - 2, thumb_h, accent, 1);
    }

    window_present(win);
}

void ntux_user_entry(void) {
    window_t win = 0x42524F575345525Full;
    if (window_init() != 0 || window_create(win, 60, 60, 820, 600, 0xFF0C131Au, "Browser") != 0) {
        sys_exit(1);
    }
    (void)window_set_icon(win, "/boot/res/icons/browser.bmp");

    // Get view height from window
    int win_h = 600;
    // y=6, tab_h=22, offset=8, addr_h=22, offset=8, bottom=10
    int view_h = win_h - 6 - 22 - 8 - 22 - 8 - 10;

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

        int view_x = 10;
        int view_y = addr_y + addr_h + 10;
    int view_w = 780;
    int line_height = 10;
        int hover_view = text_hit(mx, my, view_x, view_y, view_w, view_h);

        // Mouse wheel for scroll
        if (st.mouse_scroll > 0) { g_tabs[g_tab_active].scroll_y -= 3; if (g_tabs[g_tab_active].scroll_y < 0) g_tabs[g_tab_active].scroll_y = 0; }
        if (st.mouse_scroll < 0) { g_tabs[g_tab_active].scroll_y += 3; if (g_tabs[g_tab_active].scroll_y > g_tabs[g_tab_active].line_count - 1) g_tabs[g_tab_active].scroll_y = g_tabs[g_tab_active].line_count - 1; if (g_tabs[g_tab_active].scroll_y < 0) g_tabs[g_tab_active].scroll_y = 0; }

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
                g_focused_input = NULL;
            } else if (hover_view) {
                g_addr_focus = 0;
                // Check for link clicks and input clicks
                int clicked_line = (my - view_y - 8) / line_height + g_tabs[g_tab_active].scroll_y;
                if (clicked_line >= 0 && clicked_line < g_tabs[g_tab_active].line_count) {
                    // Check if clicked on an input field
                    dom_node_t* inp = g_tabs[g_tab_active].line_input[clicked_line];
                    if (inp) {
                        g_focused_input = inp;
                        g_input_cursor_timer = 0;
                    } else {
                        uint32_t link_node = g_tabs[g_tab_active].line_link[clicked_line];
                        if (link_node) {
                            dom_node_t* n = (dom_node_t*)(uintptr_t)link_node;
                            const char* href = find_attr(n, "href");
                            if (href) {
                                g_focused_input = NULL;
                                char full_url[URL_MAX];
                                if (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0) {
                                    strncpy(full_url, href, sizeof(full_url) - 1);
                                } else if (href[0] == '/') {
                                    const char* base = g_tabs[g_tab_active].url;
                                    // Extract scheme+host from current URL
                                    if (strncmp(base, "http://", 7) == 0) {
                                        strncpy(full_url, "http://", sizeof(full_url) - 1);
                                        int bi = 7;
                                        while (base[bi] && base[bi] != '/' && bi < (int)sizeof(full_url) - 3) { full_url[bi] = base[bi]; bi++; }
                                        full_url[bi] = '\0';
                                        int hi = 0;
                                        while (href[hi] && bi < (int)sizeof(full_url) - 2) full_url[bi++] = href[hi++];
                                        full_url[bi] = '\0';
                                    } else {
                                        strncpy(full_url, href, sizeof(full_url) - 1);
                                    }
                                } else {
                                    char* last_slash = strrchr(g_tabs[g_tab_active].url, '/');
                                    if (last_slash && last_slash > g_tabs[g_tab_active].url + 7) {
                                        int base_len = (int)(last_slash - g_tabs[g_tab_active].url + 1);
                                        memcpy(full_url, g_tabs[g_tab_active].url, (size_t)base_len);
                                        full_url[base_len] = '\0';
                                        int hi = 0;
                                        while (href[hi] && base_len + hi < (int)sizeof(full_url) - 2) { full_url[base_len + hi] = href[hi]; hi++; }
                                        full_url[base_len + hi] = '\0';
                                    } else {
                                        strncpy(full_url, href, sizeof(full_url) - 1);
                                    }
                                }
                                full_url[sizeof(full_url) - 1] = '\0';
                                strncpy(g_addr_input, full_url, sizeof(g_addr_input) - 1);
                                g_addr_input[sizeof(g_addr_input) - 1] = '\0';
                                tab_navigate(&g_tabs[g_tab_active], full_url);
                            }
                        } else {
                            g_focused_input = NULL;
                        }
                    }
                }
            } else {
                g_addr_focus = 0;
                g_focused_input = NULL;
            }
        }

        // Keyboard input (sys_getchar is non-blocking, returns -1 if no key)
        long ch = sys_getchar();
        if (ch > 0) {
            if (g_focused_input) {
                if (ch == '\n' || ch == '\r') {
                    // Submit form - look for parent <form> and navigate to action URL
                    const char* action = find_attr(g_focused_input, "formaction");
                    if (!action) {
                        dom_node_t* p = g_focused_input->parent;
                        while (p) {
                            if (ci_eq(p->tag, "form")) { action = find_attr(p, "action"); break; }
                            p = p->parent;
                        }
                    }
                    if (action) {
                        char full_url[URL_MAX];
                        if (strncmp(action, "http://", 7) == 0 || strncmp(action, "https://", 8) == 0) {
                            strncpy(full_url, action, sizeof(full_url) - 1);
                        } else if (action[0] == '/') {
                            const char* base = g_tabs[g_tab_active].url;
                            if (strncmp(base, "http://", 7) == 0) {
                                strncpy(full_url, "http://", sizeof(full_url) - 1);
                                int bi = 7;
                                while (base[bi] && base[bi] != '/' && bi < (int)sizeof(full_url) - 3) { full_url[bi] = base[bi]; bi++; }
                                full_url[bi] = '\0';
                                int hi = 0;
                                while (action[hi] && bi < (int)sizeof(full_url) - 2) full_url[bi++] = action[hi++];
                                full_url[bi] = '\0';
                            } else { strncpy(full_url, action, sizeof(full_url) - 1); }
                        } else {
                            char* last_slash = strrchr(g_tabs[g_tab_active].url, '/');
                            if (last_slash && last_slash > g_tabs[g_tab_active].url + 7) {
                                int base_len = (int)(last_slash - g_tabs[g_tab_active].url + 1);
                                memcpy(full_url, g_tabs[g_tab_active].url, (size_t)base_len);
                                full_url[base_len] = '\0';
                                int hi = 0;
                                while (action[hi] && base_len + hi < (int)sizeof(full_url) - 2) { full_url[base_len + hi] = action[hi]; hi++; }
                                full_url[base_len + hi] = '\0';
                            } else { strncpy(full_url, action, sizeof(full_url) - 1); }
                        }
                        full_url[sizeof(full_url) - 1] = '\0';
                        strncpy(g_addr_input, full_url, sizeof(g_addr_input) - 1);
                        g_addr_input[sizeof(g_addr_input) - 1] = '\0';
                        tab_navigate(&g_tabs[g_tab_active], full_url);
                    }
                    g_focused_input = NULL;
                } else if (ch == 8 || ch == 127) {
                    char* v = NULL;
                    for (int ai = 0; ai < g_focused_input->attr_count; ai++) {
                        if (ci_eq(g_focused_input->attrs[ai].name, "value")) { v = g_focused_input->attrs[ai].value; break; }
                    }
                    if (v) {
                        size_t len = strlen(v);
                        if (len > 0) { v[len - 1] = '\0'; }
                    }
                    tab_set_content(&g_tabs[g_tab_active], g_tabs[g_tab_active].content);
                } else if (ch == '\t' || ch == 0x51) {
                    // Tab / F3: blur input
                    g_focused_input = NULL;
                } else if (ch >= 32 && ch <= 126) {
                    char* v = NULL;
                    int vi = 0;
                    // Find or create value attribute
                    for (int ai = 0; ai < g_focused_input->attr_count; ai++) {
                        if (ci_eq(g_focused_input->attrs[ai].name, "value")) { v = g_focused_input->attrs[ai].value; vi = (int)strlen(v); break; }
                    }
                    if (!v && g_focused_input->attr_count < DOM_ATTR_MAX) {
                        int ai = g_focused_input->attr_count++;
                        strcpy(g_focused_input->attrs[ai].name, "value");
                        g_focused_input->attrs[ai].value[0] = '\0';
                        v = g_focused_input->attrs[ai].value;
                        vi = 0;
                    }
                    if (v && vi < 120) { v[vi] = (char)ch; v[vi + 1] = '\0'; }
                    tab_set_content(&g_tabs[g_tab_active], g_tabs[g_tab_active].content);
                }
            } else if (g_addr_focus) {
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
            } else {
                if (ch == 'j' || ch == 0x50) {
                    g_tabs[g_tab_active].scroll_y += 1;
                    if (g_tabs[g_tab_active].scroll_y > g_tabs[g_tab_active].line_count - 1)
                        g_tabs[g_tab_active].scroll_y = g_tabs[g_tab_active].line_count - 1;
                    if (g_tabs[g_tab_active].scroll_y < 0) g_tabs[g_tab_active].scroll_y = 0;
                }
                if (ch == 'k' || ch == 0x48) {
                    g_tabs[g_tab_active].scroll_y -= 1;
                    if (g_tabs[g_tab_active].scroll_y < 0) g_tabs[g_tab_active].scroll_y = 0;
                }
            }
        }

        // Cursor blink for focused input
        g_input_cursor_timer++;
        if (g_input_cursor_timer > 10) {
            g_input_cursor_timer = 0;
            g_input_cursor_visible = !g_input_cursor_visible;
        }

        draw_ui(win, &st, hover_tab, hover_btn, hover_addr, view_h);
        last_left = st.mouse_left;
        sys_wait_ticks(1);
    }

    for (int i = 0; i < g_tab_count; i++) {
        if (g_tabs[i].dom_root) dom_free(g_tabs[i].dom_root);
    }
    window_close(win);
    sys_exit(0);
}
