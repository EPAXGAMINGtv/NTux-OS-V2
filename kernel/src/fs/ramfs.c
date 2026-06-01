#include <fs/ramfs.h>

#include <lib/string.h>

static int ramfs_alloc_node(ramfs_t* fs) {
    for (int i = 0; i < RAMFS_MAX_NODES; ++i) {
        if (!fs->nodes[i].used) {
            memset(&fs->nodes[i], 0, sizeof(ramfs_node_t));
            fs->nodes[i].used = 1;
            return i;
        }
    }
    return -1;
}

static void ramfs_copy_name(char* out, const char* name) {
    size_t i = 0;
    while (i + 1 < VFS_MAX_NAME && name[i] != '\0') {
        out[i] = name[i];
        ++i;
    }
    out[i] = '\0';
}

static int ramfs_path_next(const char** p, char token[VFS_MAX_NAME]) {
    const char* s = *p;
    while (*s == '/') {
        ++s;
    }
    if (*s == '\0') {
        *p = s;
        token[0] = '\0';
        return 0;
    }

    size_t i = 0;
    while (*s != '\0' && *s != '/' && i + 1 < VFS_MAX_NAME) {
        token[i++] = *s++;
    }
    token[i] = '\0';

    while (*s != '\0' && *s != '/') {
        ++s;
    }
    *p = s;
    return 1;
}

static int ramfs_find_child(const ramfs_t* fs, int parent_index, const char* name) {
    const ramfs_node_t* parent = &fs->nodes[parent_index];
    for (size_t i = 0; i < parent->child_count; ++i) {
        int cidx = parent->children[i];
        const ramfs_node_t* c = &fs->nodes[cidx];
        if (c->used && strcmp(c->name, name) == 0) {
            return cidx;
        }
    }
    return -1;
}

static int ramfs_create_child(ramfs_t* fs, int parent_index, const char* name, ramfs_node_type_t type) {
    ramfs_node_t* parent = &fs->nodes[parent_index];
    if (parent->child_count >= RAMFS_MAX_CHILDREN) {
        return -1;
    }

    int idx = ramfs_alloc_node(fs);
    if (idx < 0) {
        return -2;
    }

    ramfs_node_t* n = &fs->nodes[idx];
    n->type = type;
    n->parent = parent_index;
    ramfs_copy_name(n->name, name);

    parent->children[parent->child_count++] = idx;
    return idx;
}

static int ramfs_resolve(ramfs_t* fs, const char* path, int create_dirs, int* out_parent, char out_leaf[VFS_MAX_NAME]) {
    if (path == NULL || path[0] == '\0' || path[0] != '/') {
        return -1;
    }

    int cur = fs->root_index;
    const char* p = path;
    char tok[VFS_MAX_NAME];
    char next[VFS_MAX_NAME];

    if (!ramfs_path_next(&p, tok)) {
        if (out_parent) *out_parent = fs->root_index;
        if (out_leaf) out_leaf[0] = '\0';
        return fs->root_index;
    }

    while (1) {
        const char* save = p;
        int has_next = ramfs_path_next(&save, next);
        int child = ramfs_find_child(fs, cur, tok);

        if (!has_next) {
            if (out_parent) *out_parent = cur;
            if (out_leaf) ramfs_copy_name(out_leaf, tok);
            if (child >= 0) {
                return child;
            }
            return -2;
        }

        if (child < 0) {
            if (!create_dirs) {
                return -3;
            }
            child = ramfs_create_child(fs, cur, tok, RAMFS_NODE_DIR);
            if (child < 0) {
                return -4;
            }
        }

        if (fs->nodes[child].type != RAMFS_NODE_DIR) {
            return -5;
        }

        cur = child;
        p = save;
        ramfs_copy_name(tok, next);
    }
}

static uint8_t* ramfs_alloc_bytes(ramfs_t* fs, size_t len) {
    if (len == 0) {
        return NULL;
    }
    if (fs->file_pool_used + len > RAMFS_MAX_FILE_BYTES) {
        return NULL;
    }
    uint8_t* ptr = &fs->file_pool[fs->file_pool_used];
    fs->file_pool_used += len;
    return ptr;
}

static int ramfs_op_mkdir(void* ctx, const char* path) {
    ramfs_t* fs = (ramfs_t*)ctx;
    int parent = -1;
    char leaf[VFS_MAX_NAME];
    int rc = ramfs_resolve(fs, path, 1, &parent, leaf);

    if (rc >= 0) {
        return fs->nodes[rc].type == RAMFS_NODE_DIR ? 0 : -1;
    }

    if (rc == -2) {
        int created = ramfs_create_child(fs, parent, leaf, RAMFS_NODE_DIR);
        return created >= 0 ? 0 : -2;
    }

    return -3;
}

static int ramfs_op_create_file(void* ctx, const char* path, const void* data, size_t len) {
    ramfs_t* fs = (ramfs_t*)ctx;
    int parent = -1;
    char leaf[VFS_MAX_NAME];
    int rc = ramfs_resolve(fs, path, 1, &parent, leaf);

    int node = rc;
    if (rc == -2) {
        node = ramfs_create_child(fs, parent, leaf, RAMFS_NODE_FILE);
        if (node < 0) {
            return -1;
        }
    } else if (rc < 0) {
        return -2;
    }

    ramfs_node_t* n = &fs->nodes[node];
    if (n->type != RAMFS_NODE_FILE) {
        return -3;
    }

    if (len == 0) {
        n->data = NULL;
        n->size = 0;
        n->capacity = 0;
        return 0;
    }

    uint8_t* dst = n->capacity >= len ? n->data : ramfs_alloc_bytes(fs, len);
    if (dst == NULL) {
        return -4;
    }

    memcpy(dst, data, len);
    n->data = dst;
    n->size = len;
    n->capacity = len;
    return 0;
}

static int ramfs_op_write_file(void* ctx, const char* path, const void* data, size_t len) {
    return ramfs_op_create_file(ctx, path, data, len);
}

static int ramfs_op_read_file(void* ctx, const char* path, void* out, size_t out_cap, size_t* out_len) {
    ramfs_t* fs = (ramfs_t*)ctx;
    int rc = ramfs_resolve(fs, path, 0, NULL, NULL);
    if (rc < 0) {
        return -1;
    }

    const ramfs_node_t* n = &fs->nodes[rc];
    if (n->type != RAMFS_NODE_FILE) {
        return -2;
    }

    if (out_len) {
        *out_len = n->size;
    }

    if (out == NULL) {
        return 0;
    }

    if (out_cap < n->size) {
        return -3;
    }

    if (n->size > 0) {
        memcpy(out, n->data, n->size);
    }

    return 0;
}

int ramfs_get_file_view(ramfs_t* fs, const char* path, const uint8_t** out_data, size_t* out_len) {
    if (!fs || !path || path[0] != '/' || !out_data || !out_len) return -1;
    int rc = ramfs_resolve(fs, path, 0, NULL, NULL);
    if (rc < 0) return -1;
    const ramfs_node_t* n = &fs->nodes[rc];
    if (n->type != RAMFS_NODE_FILE) return -2;
    *out_data = n->data;
    *out_len = n->size;
    return 0;
}

static int ramfs_op_list_dir(void* ctx, const char* path, vfs_dirent_t* out, size_t max_entries, size_t* out_count) {
    ramfs_t* fs = (ramfs_t*)ctx;
    int rc = ramfs_resolve(fs, path, 0, NULL, NULL);
    if (rc < 0) {
        return -1;
    }

    const ramfs_node_t* dir = &fs->nodes[rc];
    if (dir->type != RAMFS_NODE_DIR) {
        return -2;
    }

    size_t n = dir->child_count;
    if (out_count) {
        *out_count = n;
    }

    if (out == NULL || max_entries == 0) {
        return 0;
    }

    size_t copy_count = n < max_entries ? n : max_entries;
    for (size_t i = 0; i < copy_count; ++i) {
        int cidx = dir->children[i];
        const ramfs_node_t* c = &fs->nodes[cidx];
        memset(&out[i], 0, sizeof(vfs_dirent_t));
        ramfs_copy_name(out[i].name, c->name);
        out[i].is_dir = (c->type == RAMFS_NODE_DIR);
        out[i].size = c->size;
    }

    return 0;
}

static int ramfs_op_exists(void* ctx, const char* path) {
    ramfs_t* fs = (ramfs_t*)ctx;
    int rc = ramfs_resolve(fs, path, 0, NULL, NULL);
    return rc >= 0 ? 1 : 0;
}

static int ramfs_dir_is_empty(const ramfs_t* fs, int node) {
    if (node < 0 || node >= RAMFS_MAX_NODES) return 0;
    const ramfs_node_t* n = &fs->nodes[node];
    if (n->type != RAMFS_NODE_DIR) return 0;
    return n->child_count == 0;
}

static int ramfs_detach_child(ramfs_t* fs, int parent, int child) {
    if (parent < 0 || parent >= RAMFS_MAX_NODES) return -1;
    ramfs_node_t* p = &fs->nodes[parent];
    size_t idx = (size_t)-1;
    for (size_t i = 0; i < p->child_count; ++i) {
        if (p->children[i] == child) {
            idx = i;
            break;
        }
    }
    if (idx == (size_t)-1) return -1;
    for (size_t i = idx; i + 1 < p->child_count; ++i) {
        p->children[i] = p->children[i + 1];
    }
    if (p->child_count > 0) p->child_count--;
    return 0;
}

static int ramfs_op_remove(void* ctx, const char* path) {
    ramfs_t* fs = (ramfs_t*)ctx;
    if (!path || path[0] != '/' || (path[1] == '\0')) return -1;

    int parent = -1;
    char leaf[VFS_MAX_NAME];
    int node = ramfs_resolve(fs, path, 0, &parent, leaf);
    if (node < 0 || parent < 0) return -1;

    ramfs_node_t* n = &fs->nodes[node];
    if (n->type == RAMFS_NODE_DIR && !ramfs_dir_is_empty(fs, node)) return -1;
    if (ramfs_detach_child(fs, parent, node) != 0) return -1;
    memset(n, 0, sizeof(*n));
    return 0;
}

static int ramfs_op_rename(void* ctx, const char* old_path, const char* new_path) {
    ramfs_t* fs = (ramfs_t*)ctx;
    if (!old_path || !new_path || old_path[0] != '/' || new_path[0] != '/') return -1;
    if (strcmp(old_path, "/") == 0 || strcmp(new_path, "/") == 0) return -1;

    int old_parent = -1;
    char old_leaf[VFS_MAX_NAME];
    int node = ramfs_resolve(fs, old_path, 0, &old_parent, old_leaf);
    if (node < 0) return -1;

    int new_parent = -1;
    char new_leaf[VFS_MAX_NAME];
    int target = ramfs_resolve(fs, new_path, 0, &new_parent, new_leaf);
    if (new_parent < 0) return -1;
    if (target >= 0) return -1;

    if (old_parent != new_parent) {
        if (ramfs_detach_child(fs, old_parent, node) != 0) return -1;
        ramfs_node_t* np = &fs->nodes[new_parent];
        if (np->child_count >= RAMFS_MAX_CHILDREN) return -1;
        np->children[np->child_count++] = node;
        fs->nodes[node].parent = new_parent;
    }
    ramfs_copy_name(fs->nodes[node].name, new_leaf);
    return 0;
}

static const vfs_backend_ops_t g_ramfs_ops = {
    .mkdir = ramfs_op_mkdir,
    .create_file = ramfs_op_create_file,
    .write_file = ramfs_op_write_file,
    .read_file = ramfs_op_read_file,
    .list_dir = ramfs_op_list_dir,
    .exists = ramfs_op_exists,
    .remove = ramfs_op_remove,
    .rename = ramfs_op_rename,
};

void ramfs_init(ramfs_t* fs) {
    memset(fs, 0, sizeof(*fs));
    int root = ramfs_alloc_node(fs);
    fs->root_index = root;
    fs->nodes[root].type = RAMFS_NODE_DIR;
    fs->nodes[root].parent = root;
    fs->nodes[root].name[0] = '/';
    fs->nodes[root].name[1] = '\0';
}

const vfs_backend_ops_t* ramfs_backend_ops(void) {
    return &g_ramfs_ops;
}
