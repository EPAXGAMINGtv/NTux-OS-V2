#include "user.h"
#include <fs/fs.h>
#include <lib/string.h>
#include <lib/kutils.h>
#include <drivers/framebuffer/kprint.h>
#include <mm/kmalloc.h>

static user_entry_t g_users[USER_DB_MAX];
static int g_user_count = 0;
static group_entry_t g_groups[USER_DB_MAX];
static int g_group_count = 0;

static void utoa(uint32_t val, char* out) {
    char tmp[16];
    int pos = 0;
    if (val == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    while (val > 0 && pos < 15) {
        tmp[pos++] = (char)('0' + (val % 10));
        val /= 10;
    }
    for (int i = 0; i < pos; ++i)
        out[i] = tmp[pos - 1 - i];
    out[pos] = '\0';
}

static uint64_t fnv1a_hash(const char* data) {
    uint64_t h = 1469598103934665603ull;
    char c;
    while ((c = *data++) != '\0') {
        h ^= (uint8_t)c;
        h *= 1099511628211ull;
    }
    return h;
}

static void bytes_to_hex(const uint8_t* in, size_t in_len, char* out) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < in_len; ++i) {
        out[i * 2] = hex[(in[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex[in[i] & 0xF];
    }
    out[in_len * 2] = '\0';
}

static void compute_hash(const char* name, const char* password, char* out_hash) {
    size_t nlen = strlen(name);
    size_t plen = strlen(password);
    size_t total_len = nlen + 1 + plen;
    char* buf = (char*)kmalloc(total_len + 1);
    if (!buf) return;
    memcpy(buf, name, nlen);
    buf[nlen] = ':';
    memcpy(buf + nlen + 1, password, plen);
    buf[total_len] = '\0';
    uint64_t h = fnv1a_hash(buf);
    kfree(buf);
    uint8_t hash_bytes[8];
    for (int i = 0; i < 8; ++i)
        hash_bytes[i] = (uint8_t)(h >> (56 - i * 8));
    bytes_to_hex(hash_bytes, 8, out_hash);
}

static int load_passwd(void) {
    size_t len = 0;
    char* data = NULL;
    if (fs_read_file("/etc/passwd", NULL, 0, &len) != 0) return -1;
    if (len == 0) return -1;
    data = (char*)kmalloc(len + 1);
    if (!data) return -1;
    if (fs_read_file("/etc/passwd", data, len, &len) != 0) {
        kfree(data);
        return -1;
    }
    data[len] = '\0';

    char* line = data;
    int count = 0;
    while (*line && count < USER_DB_MAX) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        char* name = line;
        char* uid_s = strchr(name, ':');
        if (!uid_s) { if (nl) { line = nl + 1; } else break; continue; }
        *uid_s++ = '\0';
        char* gid_s = strchr(uid_s, ':');
        if (!gid_s) { if (nl) { line = nl + 1; } else break; continue; }
        *gid_s++ = '\0';
        char* home = strchr(gid_s, ':');
        if (!home) { if (nl) { line = nl + 1; } else break; continue; }
        *home++ = '\0';
        char* shell = strchr(home, ':');
        if (!shell) { if (nl) { line = nl + 1; } else break; continue; }
        *shell++ = '\0';

        strncpy(g_users[count].name, name, sizeof(g_users[count].name) - 1);
        g_users[count].uid = (uint32_t)strtol(uid_s, DEC);
        g_users[count].gid = (uint32_t)strtol(gid_s, DEC);
        strncpy(g_users[count].home, home, sizeof(g_users[count].home) - 1);
        strncpy(g_users[count].shell, shell, sizeof(g_users[count].shell) - 1);
        memset(g_users[count].passwd_hash, 0, sizeof(g_users[count].passwd_hash));

        count++;
        if (nl) line = nl + 1; else break;
    }
    g_user_count = count;
    kfree(data);
    return 0;
}

static int load_shadow(void) {
    size_t len = 0;
    char* data = NULL;
    if (fs_read_file("/etc/shadow", NULL, 0, &len) != 0) return -1;
    if (len == 0) return -1;
    data = (char*)kmalloc(len + 1);
    if (!data) return -1;
    if (fs_read_file("/etc/shadow", data, len, &len) != 0) {
        kfree(data);
        return -1;
    }
    data[len] = '\0';

    char* line = data;
    while (*line) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        char* name = line;
        char* hash = strchr(name, ':');
        if (!hash) { if (nl) { line = nl + 1; } else break; continue; }
        *hash++ = '\0';

        for (int i = 0; i < g_user_count; ++i) {
            if (strcmp(g_users[i].name, name) == 0) {
                strncpy(g_users[i].passwd_hash, hash, sizeof(g_users[i].passwd_hash) - 1);
                break;
            }
        }
        if (nl) line = nl + 1; else break;
    }
    kfree(data);
    return 0;
}

static int load_group(void) {
    size_t len = 0;
    char* data = NULL;
    if (fs_read_file("/etc/group", NULL, 0, &len) != 0) return -1;
    if (len == 0) return -1;
    data = (char*)kmalloc(len + 1);
    if (!data) return -1;
    if (fs_read_file("/etc/group", data, len, &len) != 0) {
        kfree(data);
        return -1;
    }
    data[len] = '\0';

    char* line = data;
    int count = 0;
    while (*line && count < USER_DB_MAX) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        char* name = line;
        char* gid_s = strchr(name, ':');
        if (!gid_s) { if (nl) { line = nl + 1; } else break; continue; }
        *gid_s++ = '\0';
        char* members = strchr(gid_s, ':');
        if (!members) { if (nl) { line = nl + 1; } else break; continue; }
        *members++ = '\0';

        strncpy(g_groups[count].name, name, sizeof(g_groups[count].name) - 1);
        g_groups[count].gid = (uint32_t)strtol(gid_s, DEC);
        g_groups[count].member_count = 0;

        char* m = members;
        while (*m && g_groups[count].member_count < USER_MAX_GROUPS) {
            char* comma = strchr(m, ',');
            if (comma) *comma = '\0';
            g_groups[count].members[g_groups[count].member_count++] = (uint32_t)strtol(m, DEC);
            if (comma) m = comma + 1; else break;
        }

        count++;
        if (nl) line = nl + 1; else break;
    }
    g_group_count = count;
    kfree(data);
    return 0;
}

int sys_user_load(void) {
    memset(g_users, 0, sizeof(g_users));
    g_user_count = 0;
    memset(g_groups, 0, sizeof(g_groups));
    g_group_count = 0;

    int has_passwd = (fs_exists("/etc/passwd") && load_passwd() == 0);
    int has_shadow = (fs_exists("/etc/shadow") && load_shadow() == 0);
    int has_group = (fs_exists("/etc/group") && load_group() == 0);

    if (has_passwd || has_shadow || has_group)
        return 0;
    return -1;
}

static int create_defaults(void) {
    char root_hash[USER_MAX_PASS];
    compute_hash("root", "toor", root_hash);

    int r = sys_user_add("root", 0, 0, "/root", "/bin/sh", root_hash);
    if (r != 0) return r;

    r = sys_group_add("root", 0);
    if (r != 0) return r;

    r = sys_group_add_member("root", 0);
    return r;
}

int sys_user_init(void) {
    if (sys_user_load() == 0)
        return 0;
    return create_defaults();
}

static int ensure_etc_dir(void) {
    if (!fs_exists("/etc")) {
        if (fs_mkdir("/", "etc") != 0)
            return -1;
    }
    return 0;
}

int sys_user_save(void) {
    if (ensure_etc_dir() != 0) return -1;

    {
        char* buf = (char*)kmalloc(4096);
        if (!buf) return -1;
        int pos = 0;
        for (int i = 0; i < g_user_count; ++i) {
            int n = 0;
            char uid_s[16], gid_s[16];
            utoa(g_users[i].uid, uid_s);
            utoa(g_users[i].gid, gid_s);

            char* p = g_users[i].name;
            while (*p && pos < 4090) buf[pos++] = *p++;
            if (pos < 4090) buf[pos++] = ':';
            p = uid_s; while (*p && pos < 4090) buf[pos++] = *p++;
            if (pos < 4090) buf[pos++] = ':';
            p = gid_s; while (*p && pos < 4090) buf[pos++] = *p++;
            if (pos < 4090) buf[pos++] = ':';
            p = g_users[i].home; while (*p && pos < 4090) buf[pos++] = *p++;
            if (pos < 4090) buf[pos++] = ':';
            p = g_users[i].shell; while (*p && pos < 4090) buf[pos++] = *p++;
            if (pos < 4090) buf[pos++] = '\n';
        }
        int exists = fs_exists("/etc/passwd");
        if (exists) {
            fs_write_file("/etc/passwd", buf, (size_t)pos);
        } else {
            fs_create_file("/etc", "passwd", buf, (size_t)pos);
        }
        kfree(buf);
    }

    {
        char* buf = (char*)kmalloc(4096);
        if (!buf) return -1;
        int pos = 0;
        for (int i = 0; i < g_user_count; ++i) {
            char* p = g_users[i].name;
            while (*p && pos < 4090) buf[pos++] = *p++;
            if (pos < 4090) buf[pos++] = ':';
            p = g_users[i].passwd_hash;
            while (*p && pos < 4090) buf[pos++] = *p++;
            if (pos < 4090) buf[pos++] = '\n';
        }
        int exists = fs_exists("/etc/shadow");
        if (exists) {
            fs_write_file("/etc/shadow", buf, (size_t)pos);
        } else {
            fs_create_file("/etc", "shadow", buf, (size_t)pos);
        }
        kfree(buf);
    }

    {
        char* buf = (char*)kmalloc(4096);
        if (!buf) return -1;
        int pos = 0;
        for (int i = 0; i < g_group_count; ++i) {
            char gid_s[16];
            utoa(g_groups[i].gid, gid_s);
            char* p = g_groups[i].name;
            while (*p && pos < 4090) buf[pos++] = *p++;
            if (pos < 4090) buf[pos++] = ':';
            p = gid_s; while (*p && pos < 4090) buf[pos++] = *p++;
            if (pos < 4090) buf[pos++] = ':';
            for (int j = 0; j < g_groups[i].member_count; ++j) {
                if (j > 0 && pos < 4090) buf[pos++] = ',';
                char uid_s[16];
                utoa(g_groups[i].members[j], uid_s);
                p = uid_s; while (*p && pos < 4090) buf[pos++] = *p++;
            }
            if (pos < 4090) buf[pos++] = '\n';
        }
        int exists = fs_exists("/etc/group");
        if (exists) {
            fs_write_file("/etc/group", buf, (size_t)pos);
        } else {
            fs_create_file("/etc", "group", buf, (size_t)pos);
        }
        kfree(buf);
    }

    return 0;
}

int sys_user_add(const char* name, uint32_t uid, uint32_t gid, const char* home, const char* shell, const char* hash) {
    if (!name || !name[0] || g_user_count >= USER_DB_MAX)
        return -1;
    for (int i = 0; i < g_user_count; ++i) {
        if (strcmp(g_users[i].name, name) == 0)
            return -1;
    }
    int idx = g_user_count++;
    strncpy(g_users[idx].name, name, sizeof(g_users[idx].name) - 1);
    g_users[idx].uid = uid;
    g_users[idx].gid = gid;
    strncpy(g_users[idx].home, home ? home : "/", sizeof(g_users[idx].home) - 1);
    strncpy(g_users[idx].shell, shell ? shell : "/bin/sh", sizeof(g_users[idx].shell) - 1);
    memset(g_users[idx].passwd_hash, 0, sizeof(g_users[idx].passwd_hash));
    if (hash)
        strncpy(g_users[idx].passwd_hash, hash, sizeof(g_users[idx].passwd_hash) - 1);
    sys_user_save();
    return 0;
}

int sys_user_del(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < g_user_count; ++i) {
        if (strcmp(g_users[i].name, name) == 0) {
            int remaining = g_user_count - i - 1;
            if (remaining > 0)
                memmove(&g_users[i], &g_users[i + 1], (size_t)remaining * sizeof(user_entry_t));
            g_user_count--;
            sys_user_save();
            return 0;
        }
    }
    return -1;
}

int sys_user_set_hash(const char* name, const char* hash) {
    if (!name || !hash) return -1;
    for (int i = 0; i < g_user_count; ++i) {
        if (strcmp(g_users[i].name, name) == 0) {
            strncpy(g_users[i].passwd_hash, hash, sizeof(g_users[i].passwd_hash) - 1);
            sys_user_save();
            return 0;
        }
    }
    return -1;
}

const user_entry_t* sys_user_get_by_name(const char* name) {
    if (!name) return NULL;
    for (int i = 0; i < g_user_count; ++i) {
        if (strcmp(g_users[i].name, name) == 0)
            return &g_users[i];
    }
    return NULL;
}

const user_entry_t* sys_user_get_by_uid(uint32_t uid) {
    for (int i = 0; i < g_user_count; ++i) {
        if (g_users[i].uid == uid)
            return &g_users[i];
    }
    return NULL;
}

int sys_user_get_count(void) {
    return g_user_count;
}

const user_entry_t* sys_user_get_all(void) {
    return g_users;
}

int sys_group_add(const char* name, uint32_t gid) {
    if (!name || !name[0] || g_group_count >= USER_DB_MAX)
        return -1;
    for (int i = 0; i < g_group_count; ++i) {
        if (strcmp(g_groups[i].name, name) == 0)
            return -1;
    }
    int idx = g_group_count++;
    strncpy(g_groups[idx].name, name, sizeof(g_groups[idx].name) - 1);
    g_groups[idx].gid = gid;
    g_groups[idx].member_count = 0;
    sys_user_save();
    return 0;
}

int sys_group_del(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < g_group_count; ++i) {
        if (strcmp(g_groups[i].name, name) == 0) {
            int remaining = g_group_count - i - 1;
            if (remaining > 0)
                memmove(&g_groups[i], &g_groups[i + 1], (size_t)remaining * sizeof(group_entry_t));
            g_group_count--;
            sys_user_save();
            return 0;
        }
    }
    return -1;
}

int sys_group_add_member(const char* group, uint32_t uid) {
    if (!group) return -1;
    for (int i = 0; i < g_group_count; ++i) {
        if (strcmp(g_groups[i].name, group) == 0) {
            if (g_groups[i].member_count >= USER_MAX_GROUPS) return -1;
            for (int j = 0; j < g_groups[i].member_count; ++j) {
                if (g_groups[i].members[j] == uid) return 0;
            }
            g_groups[i].members[g_groups[i].member_count++] = uid;
            sys_user_save();
            return 0;
        }
    }
    return -1;
}

int sys_group_del_member(const char* group, uint32_t uid) {
    if (!group) return -1;
    for (int i = 0; i < g_group_count; ++i) {
        if (strcmp(g_groups[i].name, group) == 0) {
            for (int j = 0; j < g_groups[i].member_count; ++j) {
                if (g_groups[i].members[j] == uid) {
                    int remaining = g_groups[i].member_count - j - 1;
                    if (remaining > 0)
                        memmove(&g_groups[i].members[j], &g_groups[i].members[j + 1], (size_t)remaining * sizeof(uint32_t));
                    g_groups[i].member_count--;
                    sys_user_save();
                    return 0;
                }
            }
            return -1;
        }
    }
    return -1;
}

const group_entry_t* sys_group_get_by_name(const char* name) {
    if (!name) return NULL;
    for (int i = 0; i < g_group_count; ++i) {
        if (strcmp(g_groups[i].name, name) == 0)
            return &g_groups[i];
    }
    return NULL;
}

const group_entry_t* sys_group_get_by_gid(uint32_t gid) {
    for (int i = 0; i < g_group_count; ++i) {
        if (g_groups[i].gid == gid)
            return &g_groups[i];
    }
    return NULL;
}

int sys_auth_user(const char* name, const char* password) {
    if (!name || !password) return -1;
    for (int i = 0; i < g_user_count; ++i) {
        if (strcmp(g_users[i].name, name) == 0) {
            char hash[USER_MAX_PASS];
            compute_hash(name, password, hash);
            if (strcmp(g_users[i].passwd_hash, hash) == 0)
                return 0;
            return -1;
        }
    }
    return -1;
}

uint32_t sys_get_default_gid(void) {
    for (int i = 0; i < g_group_count; ++i) {
        if (g_groups[i].gid == 0)
            return 0;
    }
    return 100;
}
