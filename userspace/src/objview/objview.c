#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <syscall.h>
#include <window.h>

typedef struct { float x, y, z; } vec3;
typedef struct { int a, b, c; } tri;

static uint8_t g_key_last[128];

static int key_edge(int sc) {
    int now = (sys_kbd_is_pressed((uint8_t)sc) > 0) ? 1 : 0;
    int pressed = (now && !g_key_last[sc]) ? 1 : 0;
    g_key_last[sc] = (uint8_t)now;
    return pressed;
}

static int read_start_path(char* out, size_t cap) {
    uint64_t len = 0;
    if (!out || cap == 0) return -1;
    out[0] = '\0';
    if (sys_fs_read_file("/tmp/objview_path", 0, 0, &len) != 0 || len == 0 || len >= cap) return -1;
    if (sys_fs_read_file("/tmp/objview_path", out, len, &len) != 0) return -1;
    if (len >= cap) len = cap - 1;
    out[len] = '\0';
    return 0;
}

static const char* path_basename_ptr(const char* path) {
    const char* last = path;
    if (!path) return "";
    for (const char* p = path; *p; ++p) {
        if (*p == '/') last = p + 1;
    }
    return last;
}

static int load_file_all(const char* path, char** out, size_t* out_len) {
    uint64_t len = 0;
    if (!out || !out_len) return -1;
    *out = 0;
    *out_len = 0;
    if (sys_fs_read_file(path, 0, 0, &len) != 0 || len == 0) return -1;
    if (len > (8u * 1024u * 1024u)) return -1;
    char* buf = (char*)malloc((size_t)len + 1u);
    if (!buf) return -1;
    if (sys_fs_read_file(path, buf, len, &len) != 0) {
        free(buf);
        return -1;
    }
    buf[len] = '\0';
    *out = buf;
    *out_len = (size_t)len;
    return 0;
}

static int parse_face_indices(const char* line, int* out, int max) {
    int count = 0;
    const char* p = line;
    while (*p && count < max) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '\n' || *p == '\r') break;
        char* end = 0;
        long v = strtol(p, &end, 10);
        if (end == p) {
            while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
            continue;
        }
        out[count++] = (int)v;
        p = end;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
    }
    return count;
}

static int parse_obj(const char* data, vec3** out_v, int* out_vn, tri** out_t, int* out_tn) {
    if (!data || !out_v || !out_t || !out_vn || !out_tn) return -1;
    int vcap = 256, tcap = 512;
    int vn = 0, tn = 0;
    vec3* verts = (vec3*)malloc(sizeof(vec3) * (size_t)vcap);
    tri* tris = (tri*)malloc(sizeof(tri) * (size_t)tcap);
    if (!verts || !tris) { free(verts); free(tris); return -1; }

    const char* p = data;
    while (*p) {
        if ((p[0] == 'v' && p[1] == ' ') || (p[0] == 'v' && p[1] == '\t')) {
            float x = 0, y = 0, z = 0;
            if (sscanf(p + 2, "%f %f %f", &x, &y, &z) == 3) {
                if (vn >= vcap) {
                    vcap *= 2;
                    vec3* nv = (vec3*)realloc(verts, sizeof(vec3) * (size_t)vcap);
                    if (!nv) { free(verts); free(tris); return -1; }
                    verts = nv;
                }
                verts[vn++] = (vec3){x, y, z};
            }
        } else if ((p[0] == 'f' && p[1] == ' ') || (p[0] == 'f' && p[1] == '\t')) {
            int idx[8];
            int count = parse_face_indices(p + 1, idx, 8);
            if (count >= 3) {
                if (tn + (count - 2) >= tcap) {
                    while (tn + (count - 2) >= tcap) tcap *= 2;
                    tri* nt = (tri*)realloc(tris, sizeof(tri) * (size_t)tcap);
                    if (!nt) { free(verts); free(tris); return -1; }
                    tris = nt;
                }
                for (int k = 0; k < count; ++k) {
                    if (idx[k] < 0) idx[k] = vn + idx[k];
                    else idx[k] = idx[k] - 1;
                }
                for (int k = 1; k + 1 < count; ++k) {
                    tris[tn++] = (tri){idx[0], idx[k], idx[k + 1]};
                }
            }
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    *out_v = verts;
    *out_vn = vn;
    *out_t = tris;
    *out_tn = tn;
    return 0;
}

static void normalize_model(vec3* v, int vn) {
    if (!v || vn <= 0) return;
    float minx = v[0].x, miny = v[0].y, minz = v[0].z;
    float maxx = v[0].x, maxy = v[0].y, maxz = v[0].z;
    for (int i = 1; i < vn; ++i) {
        if (v[i].x < minx) minx = v[i].x;
        if (v[i].y < miny) miny = v[i].y;
        if (v[i].z < minz) minz = v[i].z;
        if (v[i].x > maxx) maxx = v[i].x;
        if (v[i].y > maxy) maxy = v[i].y;
        if (v[i].z > maxz) maxz = v[i].z;
    }
    float cx = (minx + maxx) * 0.5f;
    float cy = (miny + maxy) * 0.5f;
    float cz = (minz + maxz) * 0.5f;
    float dx = maxx - minx;
    float dy = maxy - miny;
    float dz = maxz - minz;
    float maxd = dx;
    if (dy > maxd) maxd = dy;
    if (dz > maxd) maxd = dz;
    float scale = (maxd > 0.0001f) ? (2.0f / maxd) : 1.0f;
    for (int i = 0; i < vn; ++i) {
        v[i].x = (v[i].x - cx) * scale;
        v[i].y = (v[i].y - cy) * scale;
        v[i].z = (v[i].z - cz) * scale;
    }
}

static void build_cube(vec3** out_v, int* out_vn, tri** out_t, int* out_tn) {
    static vec3 cube_v[8] = {
        {-1.f, -1.f, -1.f}, { 1.f, -1.f, -1.f}, { 1.f,  1.f, -1.f}, {-1.f,  1.f, -1.f},
        {-1.f, -1.f,  1.f}, { 1.f, -1.f,  1.f}, { 1.f,  1.f,  1.f}, {-1.f,  1.f,  1.f}
    };
    static tri cube_t[12] = {
        {0,1,2},{0,2,3},
        {4,5,6},{4,6,7},
        {0,1,5},{0,5,4},
        {2,3,7},{2,7,6},
        {1,2,6},{1,6,5},
        {0,3,7},{0,7,4}
    };
    vec3* v = (vec3*)malloc(sizeof(cube_v));
    tri* t = (tri*)malloc(sizeof(cube_t));
    memcpy(v, cube_v, sizeof(cube_v));
    memcpy(t, cube_t, sizeof(cube_t));
    *out_v = v; *out_vn = 8;
    *out_t = t; *out_tn = 12;
}

static float fast_sin(float x) {
    const float PI = 3.14159265f;
    const float TWO_PI = 6.28318531f;
    while (x > PI) x -= TWO_PI;
    while (x < -PI) x += TWO_PI;
    float y = (4.0f / PI) * x + (-4.0f / (PI * PI)) * x * (x < 0 ? -x : x);
    float ay = y < 0 ? -y : y;
    return 0.225f * (y * ay - y) + y;
}

static float fast_cos(float x) {
    return fast_sin(x + 1.57079632f);
}

static uint32_t shade_color(uint32_t base, float k) {
    if (k < 0.15f) k = 0.15f;
    if (k > 1.0f) k = 1.0f;
    uint32_t r = (uint32_t)(((base >> 16) & 0xFFu) * k);
    uint32_t g = (uint32_t)(((base >> 8) & 0xFFu) * k);
    uint32_t b = (uint32_t)((base & 0xFFu) * k);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static void draw_filled_triangle(window_t id, int x0, int y0, int x1, int y1, int x2, int y2,
                                 int w, int h, uint32_t color) {
    if (y1 < y0) { int tx = x0, ty = y0; x0 = x1; y0 = y1; x1 = tx; y1 = ty; }
    if (y2 < y0) { int tx = x0, ty = y0; x0 = x2; y0 = y2; x2 = tx; y2 = ty; }
    if (y2 < y1) { int tx = x1, ty = y1; x1 = x2; y1 = y2; x2 = tx; y2 = ty; }

    if (y0 == y2) return;

    float dx01 = (y1 != y0) ? (float)(x1 - x0) / (float)(y1 - y0) : 0.0f;
    float dx02 = (y2 != y0) ? (float)(x2 - x0) / (float)(y2 - y0) : 0.0f;
    float dx12 = (y2 != y1) ? (float)(x2 - x1) / (float)(y2 - y1) : 0.0f;

    float xa = (float)x0;
    float xb = (float)x0;

    int y_start = y0;
    int y_end = y1;
    if (y_end < y_start) y_end = y_start;
    if (y_start < 0) {
        int dy = -y_start;
        xa += dx01 * (float)dy;
        xb += dx02 * (float)dy;
        y_start = 0;
    }
    if (y_end >= h) y_end = h - 1;
    for (int y = y_start; y <= y_end; ++y) {
        int x_start = (int)xa;
        int x_end = (int)xb;
        if (x_start > x_end) { int t = x_start; x_start = x_end; x_end = t; }
        if (x_end >= 0 && x_start < w) {
            if (x_start < 0) x_start = 0;
            if (x_end >= w) x_end = w - 1;
            window_draw_line(id, x_start, y, x_end, y, color);
        }
        xa += dx01;
        xb += dx02;
    }

    xa = (float)x1;
    xb = (float)(x0 + dx02 * (float)(y1 - y0));
    y_start = y1;
    y_end = y2;
    if (y_start < 0) {
        int dy = -y_start;
        xa += dx12 * (float)dy;
        xb += dx02 * (float)dy;
        y_start = 0;
    }
    if (y_end >= h) y_end = h - 1;
    for (int y = y_start; y <= y_end; ++y) {
        int x_start = (int)xa;
        int x_end = (int)xb;
        if (x_start > x_end) { int t = x_start; x_start = x_end; x_end = t; }
        if (x_end >= 0 && x_start < w) {
            if (x_start < 0) x_start = 0;
            if (x_end >= w) x_end = w - 1;
            window_draw_line(id, x_start, y, x_end, y, color);
        }
        xa += dx12;
        xb += dx02;
    }
}

static void project_and_draw(window_t id, const vec3* v, int vn, const tri* t, int tn,
                             float yaw, float pitch, float dist, int w, int h) {
    if (!v || !t || vn <= 0 || tn <= 0) return;
    float sy = fast_sin(yaw);
    float cy = fast_cos(yaw);
    float sp = fast_sin(pitch);
    float cp = fast_cos(pitch);

    int* sx = (int*)malloc(sizeof(int) * (size_t)vn);
    int* syy = (int*)malloc(sizeof(int) * (size_t)vn);
    float* cxv = (float*)malloc(sizeof(float) * (size_t)vn);
    float* cyv = (float*)malloc(sizeof(float) * (size_t)vn);
    float* czv = (float*)malloc(sizeof(float) * (size_t)vn);
    if (!sx || !syy || !cxv || !cyv || !czv) {
        free(sx); free(syy); free(cxv); free(cyv); free(czv);
        return;
    }

    for (int i = 0; i < vn; ++i) {
        float x = v[i].x;
        float y = v[i].y;
        float z = v[i].z;
        float x1 = x * cy + z * sy;
        float z1 = -x * sy + z * cy;
        float y1 = y * cp - z1 * sp;
        float z2 = y * sp + z1 * cp;
        cxv[i] = x1;
        cyv[i] = y1;
        czv[i] = z2;
        float inv = 1.0f / (z2 + dist);
        float px = x1 * inv;
        float py = y1 * inv;
        sx[i] = (int)(w * 0.5f + px * (float)w * 0.45f);
        syy[i] = (int)(h * 0.5f + py * (float)w * 0.45f);
    }

    typedef struct { int a, b, c; float z; } tri_sort;
    tri_sort* order = (tri_sort*)malloc(sizeof(tri_sort) * (size_t)tn);
    if (!order) { free(sx); free(syy); free(cxv); free(cyv); free(czv); return; }
    for (int i = 0; i < tn; ++i) {
        int a = t[i].a, b = t[i].b, c = t[i].c;
        float z = 0.0f;
        if (a >= 0 && b >= 0 && c >= 0 && a < vn && b < vn && c < vn) {
            z = (czv[a] + czv[b] + czv[c]) * (1.0f / 3.0f);
        }
        order[i] = (tri_sort){a, b, c, z};
    }
    for (int i = 1; i < tn; ++i) {
        tri_sort key = order[i];
        int j = i - 1;
        while (j >= 0 && order[j].z > key.z) {
            order[j + 1] = order[j];
            --j;
        }
        order[j + 1] = key;
    }

    const uint32_t base = 0xFF64D6FFu;
    for (int i = 0; i < tn; ++i) {
        int a = order[i].a, b = order[i].b, c = order[i].c;
        if (a < 0 || b < 0 || c < 0 || a >= vn || b >= vn || c >= vn) continue;
        float ax = cxv[a], ay = cyv[a], az = czv[a];
        float bx = cxv[b], by = cyv[b], bz = czv[b];
        float cx = cxv[c], cy = cyv[c], cz = czv[c];
        float abx = bx - ax, aby = by - ay, abz = bz - az;
        float acx = cx - ax, acy = cy - ay, acz = cz - az;
        float nx = aby * acz - abz * acy;
        float ny = abz * acx - abx * acz;
        float nz = abx * acy - aby * acx;
        float nl = nx * nx + ny * ny + nz * nz;
        float k = 0.6f;
        if (nl > 0.000001f) {
            float invl = 1.0f / sqrtf(nl);
            nx *= invl; ny *= invl; nz *= invl;
            k = 0.2f + 0.8f * (nx * 0.2f + ny * 0.6f + nz * 0.7f);
        }
        uint32_t col = shade_color(base, k);
        draw_filled_triangle(id, sx[a], syy[a], sx[b], syy[b], sx[c], syy[c], w, h, col);
    }

    free(sx);
    free(syy);
    free(cxv);
    free(cyv);
    free(czv);
    free(order);
}

static int load_model(const char* path, vec3** out_v, int* out_vn, tri** out_t, int* out_tn) {
    char* buf = 0;
    size_t len = 0;
    if (path && sys_fs_exists(path) > 0 && load_file_all(path, &buf, &len) == 0) {
        int rc = parse_obj(buf, out_v, out_vn, out_t, out_tn);
        free(buf);
        if (rc == 0 && *out_vn > 0 && *out_tn > 0) {
            normalize_model(*out_v, *out_vn);
            return 0;
        }
        if (*out_v) { free(*out_v); *out_v = 0; }
        if (*out_t) { free(*out_t); *out_t = 0; }
    }
    build_cube(out_v, out_vn, out_t, out_tn);
    normalize_model(*out_v, *out_vn);
    return 0;
}

void ntux_user_entry(void) {
    window_t id = 0x4F424A56494557ull; /* "OBJVIEW" */
    int w = 960, h = 640;
    if (window_init() != 0 || window_create(id, 100, 80, w, h, 0xFF0B1119u, "OBJ Viewer") != 0) {
        sys_exit(1);
    }
    (void)window_set_icon(id, "/boot/res/icons/objview.bmp");

    vec3* verts = 0;
    tri* tris = 0;
    int vn = 0, tn = 0;
    char start_path[256] = "";
    if (read_start_path(start_path, sizeof(start_path)) == 0) {
        load_model(start_path, &verts, &vn, &tris, &tn);
        window_set_title(id, path_basename_ptr(start_path));
    } else {
        load_model("/boot/res/modules/standart.obj", &verts, &vn, &tris, &tn);
    }

    float yaw = 0.4f;
    float pitch = -0.2f;
    float dist = 3.5f;
    int last_mx = 0, last_my = 0;
    int have_last = 0;

    for (;;) {
        if (window_should_close(id)) break;
        if (key_edge(0x01)) break; /* Esc */

        window_input_state_t st;
        if (window_get_input_state(id, &st) == 0 && st.focused) {
            if (!have_last) { last_mx = st.mouse_x; last_my = st.mouse_y; have_last = 1; }
            int dx = st.mouse_x - last_mx;
            int dy = st.mouse_y - last_my;
            last_mx = st.mouse_x;
            last_my = st.mouse_y;
            if (st.mouse_right) {
                yaw += (float)dx * 0.005f;
                pitch += (float)dy * 0.005f;
                if (pitch > 1.2f) pitch = 1.2f;
                if (pitch < -1.2f) pitch = -1.2f;
            }
        } else {
            have_last = 0;
        }

        if (key_edge(0x19)) { /* P */
            window_open_file_picker("Open OBJ", "/", 0);
        }

        char path[256];
        uint32_t code = 0;
        if (window_dialog_pop(path, sizeof(path), &code) == 0) {
            if (code == 1 && path[0]) {
                if (verts) free(verts);
                if (tris) free(tris);
                verts = 0; tris = 0; vn = 0; tn = 0;
                load_model(path, &verts, &vn, &tris, &tn);
                window_set_title(id, path_basename_ptr(path));
            }
        }

        window_clear(id, 0xFF0B1119u);
        window_draw_text(id, 12, 8, 0xFFBFD7FFu, "OBJ Viewer  |  RMB drag to look  |  P open");
        project_and_draw(id, verts, vn, tris, tn, yaw, pitch, dist, w, h);
        window_present(id);
        sys_wait_ticks(1);
    }

    if (verts) free(verts);
    if (tris) free(tris);
    sys_exit(0);
}
