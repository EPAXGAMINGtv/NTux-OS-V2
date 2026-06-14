#include "ttf.h"
#include <string.h>
#include <stdlib.h>

#define TTF_TAG(a,b,c,d) ((uint32_t)((a)|((b)<<8)|((c)<<16)|((d)<<24)))
#define TAG_CMAP TTF_TAG('c','m','a','p')
#define TAG_HEAD TTF_TAG('h','e','a','d')
#define TAG_HHEA TTF_TAG('h','h','e','a')
#define TAG_HMTX TTF_TAG('h','m','t','x')
#define TAG_LOCA TTF_TAG('l','o','c','a')
#define TAG_GLYF TTF_TAG('g','l','y','f')
#define TAG_MAXP TTF_TAG('m','a','x','p')
#define TAG_NAME TTF_TAG('n','a','m','e')
#define TAG_POST TTF_TAG('p','o','s','t')

static uint16_t r16(const uint8_t* p) {
    return (uint16_t)p[0] << 8 | (uint16_t)p[1];
}
static uint32_t r32(const uint8_t* p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}
static int16_t rs16(const uint8_t* p) { return (int16_t)r16(p); }

typedef struct {
    uint32_t tag;
    uint32_t offset;
    uint32_t length;
} ttf_table_t;

struct ttf_font {
    const uint8_t* data;
    size_t size;
    int num_tables;
    ttf_table_t tables[64];
    int units_per_em;
    int num_glyphs;
    int loca_format;
    int hhea_metrics;
    int16_t* advance_widths;
    int16_t* left_side_bearings;
};

static ttf_table_t* find_table(ttf_font_t* f, uint32_t tag) {
    for (int i = 0; i < f->num_tables; ++i)
        if (f->tables[i].tag == tag) return &f->tables[i];
    return 0;
}

static const uint8_t* table_data(ttf_font_t* f, ttf_table_t* t, uint32_t off, uint32_t len) {
    if (!t) return 0;
    if (off + len > t->length) return 0;
    if (t->offset + off + len > f->size) return 0;
    return f->data + t->offset + off;
}

ttf_font_t* ttf_load(const void* data, size_t size) {
    if (!data || size < 12) return 0;
    const uint8_t* p = (const uint8_t*)data;
    ttf_font_t* f = (ttf_font_t*)malloc(sizeof(ttf_font_t));
    if (!f) return 0;
    memset(f, 0, sizeof(*f));
    f->data = (const uint8_t*)data;
    f->size = size;
    uint32_t sf = r32(p);
    (void)sf;
    f->num_tables = r16(p + 4);
    if (f->num_tables > 64) { free(f); return 0; }
    for (int i = 0; i < f->num_tables; ++i) {
        int off = 12 + i * 16;
        if (off + 16 > (int)size) { free(f); return 0; }
        f->tables[i].tag = r32(p + off);
        f->tables[i].offset = r32(p + off + 8);
        f->tables[i].length = r32(p + off + 12);
    }

    ttf_table_t* head = find_table(f, TAG_HEAD);
    if (!head || head->length < 54) { free(f); return 0; }
    const uint8_t* hp = table_data(f, head, 0, 54);
    if (!hp || r32(hp + 12) != 0x5F0F3CF5) { free(f); return 0; }
    f->units_per_em = r16(hp + 18);

    ttf_table_t* maxp = find_table(f, TAG_MAXP);
    if (!maxp || maxp->length < 6) { free(f); return 0; }
    const uint8_t* mp = table_data(f, maxp, 0, 6);
    if (!mp) { free(f); return 0; }
    f->num_glyphs = r16(mp + 4);

    f->loca_format = (r16(hp + 50) & 0x100) ? 1 : 0;

    ttf_table_t* hhea = find_table(f, TAG_HHEA);
    if (!hhea || hhea->length < 36) { free(f); return 0; }
    const uint8_t* ha = table_data(f, hhea, 0, 36);
    if (!ha) { free(f); return 0; }
    f->hhea_metrics = r16(ha + 34);

    ttf_table_t* hmtx = find_table(f, TAG_HMTX);
    if (hmtx && hmtx->length >= (uint32_t)f->hhea_metrics * 4) {
        f->advance_widths = (int16_t*)calloc(f->num_glyphs, sizeof(int16_t));
        f->left_side_bearings = (int16_t*)calloc(f->num_glyphs, sizeof(int16_t));
        if (!f->advance_widths || !f->left_side_bearings) {
            free(f->advance_widths); free(f->left_side_bearings); free(f);
            return 0;
        }
        const uint8_t* hm = table_data(f, hmtx, 0, (uint32_t)f->hhea_metrics * 4);
        if (hm) {
            for (int i = 0; i < f->hhea_metrics; ++i) {
                f->advance_widths[i] = r16(hm + i * 4);
                f->left_side_bearings[i] = rs16(hm + i * 4 + 2);
            }
            int last_aw = f->advance_widths[f->hhea_metrics - 1];
            int last_lsb = f->left_side_bearings[f->hhea_metrics - 1];
            for (int i = f->hhea_metrics; i < f->num_glyphs; ++i) {
                f->advance_widths[i] = last_aw;
                f->left_side_bearings[i] = last_lsb;
            }
        }
    }
    return f;
}

void ttf_free(ttf_font_t* font) {
    if (font) {
        free(font->advance_widths);
        free(font->left_side_bearings);
        free(font);
    }
}

int ttf_glyph_index(ttf_font_t* font, uint32_t codepoint) {
    if (!font) return 0;
    ttf_table_t* cmap = find_table(font, TAG_CMAP);
    if (!cmap) return 0;
    const uint8_t* cd = table_data(font, cmap, 0, cmap->length);
    if (!cd || cmap->length < 4) return 0;
    int num_encodings = r16(cd + 2);
    for (int i = 0; i < num_encodings; ++i) {
        int eo = 4 + i * 8;
        if (eo + 8 > (int)cmap->length) break;
        uint16_t platform = r16(cd + eo);
        uint16_t encoding = r16(cd + eo + 2);
        uint32_t sub_offset = r32(cd + eo + 4);
        if (platform == 0 || (platform == 3 && encoding == 1)) {
            if (sub_offset + 2 > cmap->length) continue;
            const uint8_t* st = cd + sub_offset;
            uint16_t fmt = r16(st);
            if (fmt == 4 && sub_offset + 14 <= cmap->length) {
                int seg_count = r16(st + 6) / 2;
                const uint8_t* ec = st + 14;
                const uint8_t* sc = ec + seg_count * 2;
                const uint8_t* id_delta = sc + seg_count * 2;
                uint8_t* id_ro = (uint8_t*)(id_delta + seg_count * 2);
                for (int s = 0; s < seg_count; ++s) {
                    uint16_t end = r16(ec + s * 2);
                    if (codepoint > end) continue;
                    uint16_t start = r16(sc + s * 2);
                    if (codepoint < start) break;
                    int16_t delta = rs16(id_delta + s * 2);
                    uint16_t ro = r16(id_ro + s * 2);
                    if (ro == 0) return (int)(codepoint + delta);
                    uint32_t glyph_addr = sub_offset + (uint32_t)(ro + (codepoint - start) * 2);
                    if (glyph_addr + 2 > cmap->length) return 0;
                    return r16(cd + glyph_addr);
                }
            } else if (fmt == 0) {
                if (codepoint < 256) return st[6 + codepoint];
            } else if (fmt == 6) {
                if (sub_offset + 10 <= cmap->length) {
                    uint16_t first = r16(st + 6);
                    uint16_t count = r16(st + 8);
                    if (codepoint >= first && codepoint < first + count)
                        return r16(st + 10 + (codepoint - first) * 2);
                }
            } else if (fmt == 12) {
                if (sub_offset + 16 <= cmap->length) {
                    uint32_t n_groups = r32(st + 12);
                    for (uint32_t g = 0; g < n_groups; ++g) {
                        uint32_t off = sub_offset + 16 + g * 12;
                        if (off + 12 > cmap->length) break;
                        uint32_t sc12 = r32(cd + off);
                        uint32_t ec12 = r32(cd + off + 4);
                        uint32_t sg12 = r32(cd + off + 8);
                        if (codepoint >= sc12 && codepoint <= ec12)
                            return (int)(sg12 + (codepoint - sc12));
                    }
                }
            }
        }
    }
    return 0;
}

int ttf_advance_x(ttf_font_t* font, uint32_t codepoint, int px_size) {
    int gi = ttf_glyph_index(font, codepoint);
    if (gi < 0 || gi >= font->num_glyphs) gi = 0;
    if (font->advance_widths && gi < font->hhea_metrics)
        return font->advance_widths[gi] * px_size / font->units_per_em;
    return px_size * 6 / 10;
}

int ttf_get_height(ttf_font_t* font, int px_size) {
    ttf_table_t* hhea = find_table(font, TAG_HHEA);
    if (!hhea || hhea->length < 36) return px_size;
    const uint8_t* ha = table_data(font, hhea, 0, 36);
    if (!ha) return px_size;
    int asc = rs16(ha + 4);
    int desc = rs16(ha + 6);
    int lh = (asc - desc) * px_size / font->units_per_em;
    return lh < 1 ? 1 : lh;
}

int ttf_get_ascender(ttf_font_t* font, int px_size) {
    ttf_table_t* hhea = find_table(font, TAG_HHEA);
    if (!hhea || hhea->length < 36) return px_size * 8 / 10;
    const uint8_t* ha = table_data(font, hhea, 0, 36);
    if (!ha) return px_size * 8 / 10;
    return rs16(ha + 4) * px_size / font->units_per_em;
}

typedef struct {
    int16_t x, y;
    int on_curve;
} ttf_point_t;

typedef struct {
    ttf_point_t* pts;
    int num_pts;
    int cap;
} ttf_points_t;

static void push_pt(ttf_points_t* pts, int16_t x, int16_t y, int on_curve) {
    if (pts->num_pts >= pts->cap) {
        pts->cap = pts->cap ? pts->cap * 2 : 64;
        ttf_point_t* np = (ttf_point_t*)realloc(pts->pts, pts->cap * sizeof(ttf_point_t));
        if (!np) return;
        pts->pts = np;
    }
    pts->pts[pts->num_pts].x = x;
    pts->pts[pts->num_pts].y = y;
    pts->pts[pts->num_pts].on_curve = on_curve;
    pts->num_pts++;
}

typedef struct {
    int16_t x, y;
} ttf_i_point_t;

typedef struct {
    ttf_i_point_t* pts;
    int num_pts;
    int cap;
} ttf_i_points_t;

static void push_ipt(ttf_i_points_t* pts, int16_t x, int16_t y) {
    if (pts->num_pts >= pts->cap) {
        pts->cap = pts->cap ? pts->cap * 2 : 256;
        ttf_i_point_t* np = (ttf_i_point_t*)realloc(pts->pts, pts->cap * sizeof(ttf_i_point_t));
        if (!np) return;
        pts->pts = np;
    }
    pts->pts[pts->num_pts].x = x;
    pts->pts[pts->num_pts].y = y;
    pts->num_pts++;
}

static void bezier_split(ttf_i_point_t* out, int* n,
                         int16_t ax, int16_t ay, int16_t bx, int16_t by, int16_t cx, int16_t cy, int level) {
    if (level > 8) {
        push_ipt((ttf_i_points_t*)&(ttf_i_points_t){.pts = out, .num_pts = *n, .cap = 0}, cx, cy);
        return;
    }
    int16_t abx = (ax + bx) / 2, aby = (ay + by) / 2;
    int16_t bcx = (bx + cx) / 2, bcy = (by + cy) / 2;
    int16_t abcx = (abx + bcx) / 2, abcy = (aby + bcy) / 2;
    int dx = cx - ax, dy = cy - ay;
    int d = dx * (by - ay) - dy * (bx - ax);
    if (d < 0) d = -d;
    if (d < 2 || level >= 6) {
        push_ipt((ttf_i_points_t*)&(ttf_i_points_t){.pts = out, .num_pts = *n, .cap = 0}, abcx, abcy);
        return;
    }
    int n1 = 0, n2 = 0;
    ttf_i_point_t buf1[32], buf2[32];
    bezier_split(buf1, &n1, ax, ay, abx, aby, abcx, abcy, level + 1);
    bezier_split(buf2, &n2, abcx, abcy, bcx, bcy, cx, cy, level + 1);
    for (int i = 0; i < n1 && *n < 4096; ++i) push_ipt((ttf_i_points_t*)&(ttf_i_points_t){.pts = out, .num_pts = *n, .cap = 0}, buf1[i].x, buf1[i].y);
    for (int i = 0; i < n2 && *n < 4096; ++i) push_ipt((ttf_i_points_t*)&(ttf_i_points_t){.pts = out, .num_pts = *n, .cap = 0}, buf2[i].x, buf2[i].y);
}

ttf_glyph_t* ttf_render_glyph(ttf_font_t* font, uint32_t codepoint, int px_size) {
    if (!font || px_size < 1) return 0;
    int gi = ttf_glyph_index(font, codepoint);
    if (gi < 0 || gi >= font->num_glyphs) gi = 0;

    ttf_table_t* loca = find_table(font, TAG_LOCA);
    ttf_table_t* glyf = find_table(font, TAG_GLYF);
    if (!loca || !glyf) return 0;

    uint32_t goff, glen;
    if (font->loca_format) {
        if ((uint32_t)(gi + 1) * 4 > loca->length) return 0;
        const uint8_t* ld = table_data(font, loca, 0, (gi + 2) * 4);
        if (!ld) return 0;
        goff = r32(ld + gi * 4);
        glen = r32(ld + (gi + 1) * 4) - goff;
    } else {
        if ((uint32_t)(gi + 1) * 2 > loca->length) return 0;
        const uint8_t* ld = table_data(font, loca, 0, (gi + 2) * 2);
        if (!ld) return 0;
        goff = (uint32_t)r16(ld + gi * 2) * 2;
        glen = (uint32_t)r16(ld + (gi + 1) * 2) * 2 - goff;
    }
    if (glen == 0) {
        ttf_glyph_t* g = (ttf_glyph_t*)calloc(1, sizeof(ttf_glyph_t));
        if (g) g->advance_x = ttf_advance_x(font, codepoint, px_size);
        return g;
    }
    const uint8_t* gd = table_data(font, glyf, goff, glen);
    if (!gd || glen < 10) return 0;
    int16_t num_contours = rs16(gd);
    int16_t x_min = rs16(gd + 2);
    int16_t y_min = rs16(gd + 4);
    int16_t x_max = rs16(gd + 6);
    int16_t y_max = rs16(gd + 8);

    int upem = font->units_per_em;
    ttf_glyph_t* result = (ttf_glyph_t*)calloc(1, sizeof(ttf_glyph_t));
    if (!result) return 0;
    result->advance_x = ttf_advance_x(font, codepoint, px_size);
    if (!result->advance_x) result->advance_x = px_size * 6 / 10;

    if (num_contours <= 0) {
        return result;
    }
    int glyph_data_len = (int)glen;
    const uint8_t* end_pts = gd + 10;
    int contour_count = num_contours;
    if (contour_count * 2 + 10 > glyph_data_len) { ttf_free_glyph(result); return 0; }

    int last_pt = r16(end_pts + (contour_count - 1) * 2);
    int total_pts = last_pt + 1;
    if (total_pts > 2048) { ttf_free_glyph(result); return 0; }

    const uint8_t* insn_len_p = end_pts + contour_count * 2;
    if (insn_len_p + 2 > gd + glyph_data_len) { ttf_free_glyph(result); return 0; }
    uint16_t insn_len = r16(insn_len_p);

    const uint8_t* flags_p = insn_len_p + 2 + insn_len;
    if (flags_p > gd + glyph_data_len) { ttf_free_glyph(result); return 0; }

    uint8_t flags[2048];
    int16_t xs[2048], ys[2048];
    int flags_consumed = 0;
    int repeat = 0;
    uint8_t rflag = 0;
    for (int i = 0; i < total_pts; ++i) {
        if (repeat) { flags[i] = rflag; repeat--; }
        else {
            if (flags_p + flags_consumed >= gd + glyph_data_len) { flags[i] = 0; continue; }
            flags[i] = flags_p[flags_consumed++];
            if (flags[i] & 0x08) {
                if (flags_p + flags_consumed >= gd + glyph_data_len) { repeat = 0; continue; }
                repeat = flags_p[flags_consumed++];
                rflag = flags[i];
            }
        }
    }

    const uint8_t* xp = flags_p + flags_consumed;
    int xc = 0;
    for (int i = 0; i < total_pts; ++i) {
        if (flags[i] & 0x02) {
            if (flags[i] & 0x10) {
                if (xp + xc >= gd + glyph_data_len) xs[i] = 0;
                else xs[i] = (int16_t)(xp[xc]);
                xc++;
            } else {
                if (xp + xc >= gd + glyph_data_len) xs[i] = 0;
                else xs[i] = (int16_t)(-(int8_t)xp[xc]);
                xc++;
            }
        } else {
            if (!(flags[i] & 0x10)) {
                if (xp + xc + 1 >= gd + glyph_data_len) xs[i] = 0;
                else xs[i] = rs16(xp + xc);
                xc += 2;
            } else xs[i] = 0;
        }
    }

    const uint8_t* yp = xp + xc;
    int yc = 0;
    for (int i = 0; i < total_pts; ++i) {
        if (flags[i] & 0x04) {
            if (flags[i] & 0x20) {
                if (yp + yc >= gd + glyph_data_len) ys[i] = 0;
                else ys[i] = (int16_t)(yp[yc]);
                yc++;
            } else {
                if (yp + yc >= gd + glyph_data_len) ys[i] = 0;
                else ys[i] = (int16_t)(-(int8_t)yp[yc]);
                yc++;
            }
        } else {
            if (!(flags[i] & 0x20)) {
                if (yp + yc + 1 >= gd + glyph_data_len) ys[i] = 0;
                else ys[i] = rs16(yp + yc);
                yc += 2;
            } else ys[i] = 0;
        }
    }

    {
        int16_t acc_x = 0, acc_y = 0;
        for (int i = 0; i < total_pts; ++i) {
            int16_t nx = xs[i];
            int16_t ny = ys[i];
            if (!(flags[i] & 0x10)) { nx = (int16_t)(acc_x + nx); ny = (int16_t)(acc_y + ny); }
            else { nx = (int16_t)(acc_x + nx); ny = (int16_t)(acc_y + ny); }
            xs[i] = nx; ys[i] = ny;
            acc_x = nx; acc_y = ny;
        }
    }

    int start = 0;
    ttf_i_points_t outline = {0};
    for (int c = 0; c < contour_count; ++c) {
        int end = r16(end_pts + c * 2);
        int cur = start;
        ttf_points_t raw = {0};

        while (1) {
            int on_curve = (flags[cur] & 0x01) ? 1 : 0;
            push_pt(&raw, xs[cur], ys[cur], on_curve);
            int next = (cur == end) ? start : cur + 1;
            if (on_curve && (flags[next] & 0x01)) {
            } else if (!on_curve && !(flags[next] & 0x01)) {
                int mx = ((int)xs[cur] + xs[next]) / 2;
                int my = ((int)ys[cur] + ys[next]) / 2;
                push_pt(&raw, (int16_t)mx, (int16_t)my, 1);
            }
            if (cur == end) break;
            cur = next;
        }

        if (raw.num_pts < 2) { free(raw.pts); start = end + 1; continue; }

        for (int i = 0; i < raw.num_pts; ++i) {
            int cur_i = i;
            int next_i = (i + 1) % raw.num_pts;
            if (!raw.pts[cur_i].on_curve && !raw.pts[next_i].on_curve) {
            }
        }

        for (int i = 0; i < raw.num_pts; ++i) {
            int cur_i = i;
            int next_i = (i + 1) % raw.num_pts;
            int nx = raw.pts[cur_i].x;
            int ny = raw.pts[cur_i].y;
            int nnx = raw.pts[next_i].x;
            int nny = raw.pts[next_i].y;

            if (raw.pts[cur_i].on_curve && raw.pts[next_i].on_curve) {
                push_ipt(&outline, nx, ny);
            } else if (raw.pts[cur_i].on_curve && !raw.pts[next_i].on_curve) {
                int nni = (next_i + 1) % raw.num_pts;
                int nnx2 = raw.pts[nni].x;
                int nny2 = raw.pts[nni].y;
                int midx = (nnx + nnx2) / 2;
                int midy = (nny + nny2) / 2;
                push_ipt(&outline, nx, ny);
                int nb = 0;
                ttf_i_point_t bez_buf[32];
                bezier_split(bez_buf, &nb, nx, ny, nnx, nny, midx, midy, 0);
                for (int bi = 0; bi < nb; ++bi) push_ipt(&outline, bez_buf[bi].x, bez_buf[bi].y);
            }
        }
        free(raw.pts);
        start = end + 1;
    }

    if (outline.num_pts < 3) {
        free(outline.pts);
        result->width = 1;
        result->height = 1;
        result->bitmap = (uint8_t*)calloc(1, 1);
        return result;
    }

    int bb_x = x_min, bb_y = y_min, bb_w = x_max - x_min, bb_h = y_max - y_min;
    if (bb_w < 1) bb_w = 1;
    if (bb_h < 1) bb_h = 1;

    float scale = (float)px_size / (float)upem;
    int rw = (int)(bb_w * scale) + 2;
    int rh = (int)(bb_h * scale) + 2;
    int rx = (int)(bb_x * scale) - 1;
    int ry = (int)(bb_y * scale) - 1;
    int bearing_y = (int)(y_max * scale);

    if (rw < 1) rw = 1;
    if (rh < 1) rh = 1;
    if (rw > 512) rw = 512;
    if (rh > 512) rh = 512;

    uint8_t* bitmap = (uint8_t*)calloc((size_t)rw * rh, 1);
    if (!bitmap) { free(outline.pts); return result; }

    for (int i = 0; i < outline.num_pts; ++i) {
        int px = (int)(((float)outline.pts[i].x * scale) + 0.5f) - rx;
        int py = (int)(((float)outline.pts[i].y * scale) + 0.5f) - ry;
        outline.pts[i].x = (int16_t)px;
        outline.pts[i].y = (int16_t)py;
    }

    for (int y = 0; y < rh; ++y) {
        int crossings[512];
        int nc = 0;
        for (int i = 0; i < outline.num_pts; ++i) {
            int j = (i + 1) % outline.num_pts;
            int x1 = outline.pts[i].x, y1 = outline.pts[i].y;
            int x2 = outline.pts[j].x, y2 = outline.pts[j].y;
            if (y1 == y2) continue;
            if (y < (y1 < y2 ? y1 : y2) || y >= (y1 > y2 ? y1 : y2)) continue;
            int x_cross = x1 + (y - y1) * (x2 - x1) / (y2 - y1);
            if (nc < 512) crossings[nc++] = x_cross;
        }
        for (int i = 0; i < nc - 1; ++i) {
            for (int j = 0; j < nc - 1 - i; ++j) {
                if (crossings[j] > crossings[j + 1]) {
                    int t = crossings[j];
                    crossings[j] = crossings[j + 1];
                    crossings[j + 1] = t;
                }
            }
        }
        for (int i = 0; i + 1 < nc; i += 2) {
            int xl = crossings[i], xr = crossings[i + 1];
            if (xl < 0) xl = 0;
            if (xr >= rw) xr = rw - 1;
            for (int x = xl; x <= xr; ++x) bitmap[y * rw + x] = 0xFF;
        }
    }

    free(outline.pts);
    result->width = rw;
    result->height = rh;
    result->lsb = rx;
    result->bearing_y = bearing_y;
    result->bitmap = bitmap;
    return result;
}

void ttf_free_glyph(ttf_glyph_t* g) {
    if (g) {
        free(g->bitmap);
        free(g);
    }
}
