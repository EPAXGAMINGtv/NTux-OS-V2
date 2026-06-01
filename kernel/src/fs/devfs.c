#include <fs/devfs.h>

#include <drivers/input/input.h>

#include <lib/string.h>

#define DEVFS_MAX_NODES 16

typedef struct {
    char name[64];
    const devfs_ops_t* ops;
    void* ctx;
    int used;
} devfs_node_t;

static devfs_node_t g_nodes[DEVFS_MAX_NODES];

static int devfs_write_stub(void* ctx, const void* in, size_t len, size_t* out_written) {
    (void)ctx;
    (void)in;
    if (out_written) *out_written = len;
    return 0;
}

static int devfs_ioctl_stub(void* ctx, uint64_t req, void* arg) {
    (void)ctx;
    (void)req;
    (void)arg;
    return -1;
}

static int devfs_op_mkdir(void* ctx, const char* path) {
    (void)ctx;
    (void)path;
    return -1;
}

static int devfs_op_create(void* ctx, const char* path, const void* data, size_t len) {
    (void)ctx;
    (void)path;
    (void)data;
    (void)len;
    return -1;
}

static int devfs_op_write_file(void* ctx, const char* path, const void* data, size_t len) {
    (void)ctx;
    (void)path;
    (void)data;
    (void)len;
    return -1;
}

static int devfs_op_read_file(void* ctx, const char* path, void* out, size_t out_cap, size_t* out_len) {
    (void)ctx;
    (void)path;
    if (out_len) *out_len = 0;
    if (!out || out_cap == 0) return 0;
    return -1;
}

static int devfs_op_remove(void* ctx, const char* path) {
    (void)ctx;
    (void)path;
    return -1;
}

static int devfs_op_rename(void* ctx, const char* old_path, const char* new_path) {
    (void)ctx;
    (void)old_path;
    (void)new_path;
    return -1;
}

static int devfs_has_prefix(const char* path, const char* prefix) {
    size_t plen = strlen(prefix);
    if (strncmp(path, prefix, plen) != 0) return 0;
    if (path[plen] == '\0') return 1;
    return path[plen] == '/';
}

static int devfs_name_exists(const vfs_dirent_t* out, size_t count, size_t max_entries, const char* name) {
    if (out == NULL || max_entries == 0) return 0;
    size_t vis = count < max_entries ? count : max_entries;
    for (size_t i = 0; i < vis; ++i) {
        if (strcmp(out[i].name, name) == 0) return 1;
    }
    return 0;
}

static int devfs_emit_child(vfs_dirent_t* out, size_t max, size_t* count, const char* name, int is_dir) {
    if (out && *count < max) {
        memset(&out[*count], 0, sizeof(vfs_dirent_t));
        strncpy(out[*count].name, name, VFS_MAX_NAME - 1);
        out[*count].name[VFS_MAX_NAME - 1] = '\0';
        out[*count].is_dir = is_dir ? true : false;
        out[*count].size = 0;
    }
    (*count)++;
    return 0;
}

static int devfs_list_children(const char* path, vfs_dirent_t* out, size_t max_entries, size_t* out_count) {
    size_t count = 0;
    char child[VFS_MAX_NAME];
    for (size_t i = 0; i < DEVFS_MAX_NODES; ++i) {
        if (!g_nodes[i].used) continue;
        const char* name = g_nodes[i].name;
        if (!devfs_has_prefix(name, path)) continue;
        const char* rel = (path[0] == '\0') ? name : name + strlen(path);
        if (path[0] != '\0' && rel[0] == '/') rel++;
        const char* slash = strchr(rel, '/');
        size_t len = slash ? (size_t)(slash - rel) : strlen(rel);
        if (len == 0 || len >= sizeof(child)) continue;
        memcpy(child, rel, len);
        child[len] = '\0';
        int is_dir = (slash != NULL);
        if (devfs_name_exists(out, count, max_entries, child)) continue;
        devfs_emit_child(out, max_entries, &count, child, is_dir);
    }
    if (out_count) *out_count = count;
    return 0;
}

static int devfs_op_list_dir(void* ctx, const char* path, vfs_dirent_t* out, size_t max_entries, size_t* out_count) {
    (void)ctx;
    if (!path) return -1;
    if (strcmp(path, "/") == 0) path = "";
    return devfs_list_children(path, out, max_entries, out_count);
}

static int devfs_op_exists(void* ctx, const char* path) {
    (void)ctx;
    if (!path) return 0;
    if (strcmp(path, "/") == 0) return 1;
    if (path[0] == '\0') return 1;
    for (size_t i = 0; i < DEVFS_MAX_NODES; ++i) {
        if (!g_nodes[i].used) continue;
        if (strcmp(g_nodes[i].name, path) == 0) return 1;
        if (devfs_has_prefix(g_nodes[i].name, path)) return 1;
    }
    return 0;
}

static const vfs_backend_ops_t g_devfs_ops = {
    .mkdir = devfs_op_mkdir,
    .create_file = devfs_op_create,
    .write_file = devfs_op_write_file,
    .read_file = devfs_op_read_file,
    .list_dir = devfs_op_list_dir,
    .exists = devfs_op_exists,
    .remove = devfs_op_remove,
    .rename = devfs_op_rename
};

static int devfs_add_node(const char* name, const devfs_ops_t* ops, void* ctx) {
    if (!name || !name[0]) return -1;
    for (size_t i = 0; i < DEVFS_MAX_NODES; ++i) {
        if (!g_nodes[i].used) {
            memset(&g_nodes[i], 0, sizeof(g_nodes[i]));
            strncpy(g_nodes[i].name, name, sizeof(g_nodes[i].name) - 1);
            g_nodes[i].name[sizeof(g_nodes[i].name) - 1] = '\0';
            g_nodes[i].ops = ops;
            g_nodes[i].ctx = ctx;
            g_nodes[i].used = 1;
            return 0;
        }
    }
    return -1;
}

static int devfs_null_read(void* ctx, void* out, size_t len, size_t* out_read) {
    (void)ctx;
    (void)out;
    if (out_read) *out_read = 0;
    return (len == 0) ? 0 : 0;
}

static int devfs_zero_read(void* ctx, void* out, size_t len, size_t* out_read) {
    (void)ctx;
    if (!out) return -1;
    memset(out, 0, len);
    if (out_read) *out_read = len;
    return 0;
}

static int devfs_input_read(void* ctx, void* out, size_t len, size_t* out_read) {
    (void)ctx;
    if (!out || len < sizeof(input_event_t)) return -1;
    size_t count = len / sizeof(input_event_t);
    input_event_t* evs = (input_event_t*)out;
    size_t got = 0;
    for (; got < count; ++got) {
        int rc = input_evdev_pop(&evs[got]);
        if (rc <= 0) break;
    }
    if (out_read) *out_read = got * sizeof(input_event_t);
    return 0;
}

#define EVIOCGBIT_BASE 0x20B1
#define EVIOCGNAME_BASE 0x81006E06
#define EVIOCGID_BASE 0x80084502

static int devfs_input_ioctl(void* ctx, uint64_t req, void* arg) {
    (void)ctx;
    if (!arg) return -1;
    if ((req & 0xFFFF) == EVIOCGBIT_BASE) {
        uint8_t* dst = (uint8_t*)arg;
        uint8_t ev = (uint8_t)((req >> 16) & 0xFF);
        size_t len = (size_t)((req >> 32) & 0xFFFF);
        if (len == 0) return -1;
        memset(dst, 0, len);
        if (ev == 0) {
            dst[EV_KEY / 8] |= (1u << (EV_KEY % 8));
            dst[EV_REL / 8] |= (1u << (EV_REL % 8));
            return 0;
        }
        if (ev == EV_KEY) {
            for (int i = 0; i < 128 && (i / 8) < (int)len; ++i) {
                dst[i / 8] |= (1u << (i % 8));
            }
            dst[BTN_LEFT / 8] |= (1u << (BTN_LEFT % 8));
            dst[BTN_RIGHT / 8] |= (1u << (BTN_RIGHT % 8));
            dst[BTN_MIDDLE / 8] |= (1u << (BTN_MIDDLE % 8));
            return 0;
        }
        if (ev == EV_REL) {
            dst[REL_X / 8] |= (1u << (REL_X % 8));
            dst[REL_Y / 8] |= (1u << (REL_Y % 8));
            dst[REL_WHEEL / 8] |= (1u << (REL_WHEEL % 8));
            return 0;
        }
        return 0;
    }
    if (req == EVIOCGNAME_BASE) {
        const char* name = "NTux Input";
        strncpy((char*)arg, name, 32);
        return 0;
    }
    if (req == EVIOCGID_BASE) {
        uint16_t* id = (uint16_t*)arg;
        id[0] = 0x0000;
        id[1] = 0x0000;
        id[2] = 0x0001;
        id[3] = 0x0001;
        return 0;
    }
    return -1;
}

void devfs_init(void) {
    memset(g_nodes, 0, sizeof(g_nodes));
    static const devfs_ops_t null_ops = {
        .read = devfs_null_read,
        .write = devfs_write_stub,
        .ioctl = devfs_ioctl_stub
    };
    static const devfs_ops_t zero_ops = {
        .read = devfs_zero_read,
        .write = devfs_write_stub,
        .ioctl = devfs_ioctl_stub
    };
    static const devfs_ops_t input_ops = {
        .read = devfs_input_read,
        .write = devfs_write_stub,
        .ioctl = devfs_input_ioctl
    };
    (void)devfs_add_node("null", &null_ops, NULL);
    (void)devfs_add_node("zero", &zero_ops, NULL);
    (void)devfs_add_node("input/event0", &input_ops, NULL);
}

int devfs_register(const char* name, const devfs_ops_t* ops, void* ctx) {
    if (!ops) return -1;
    return devfs_add_node(name, ops, ctx);
}

int devfs_open(const char* name, const devfs_ops_t** out_ops, void** out_ctx) {
    if (!name || !name[0]) return -1;
    for (size_t i = 0; i < DEVFS_MAX_NODES; ++i) {
        if (!g_nodes[i].used) continue;
        if (strcmp(g_nodes[i].name, name) != 0) continue;
        if (out_ops) *out_ops = g_nodes[i].ops;
        if (out_ctx) *out_ctx = g_nodes[i].ctx;
        return 0;
    }
    return -1;
}

int devfs_list(const char* path, vfs_dirent_t* out, size_t max_entries, size_t* out_count) {
    return devfs_list_children(path ? path : "", out, max_entries, out_count);
}

int devfs_exists(const char* path) {
    return devfs_op_exists(NULL, path);
}

const vfs_backend_ops_t* devfs_backend_ops(void) {
    return &g_devfs_ops;
}
