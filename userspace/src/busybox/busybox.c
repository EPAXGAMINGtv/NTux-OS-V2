#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <args.h>

#define FILE_BUF 8192

/* -------- utility helpers -------- */
static uint64_t fnv1a64(const char *a, const char *b) {
    uint64_t h = 1469598103934665603ull;
    if (a) for (size_t i = 0; a[i]; ++i) { h ^= (uint8_t)a[i]; h *= 1099511628211ull; }
    h ^= (uint8_t)':'; h *= 1099511628211ull;
    if (b) for (size_t i = 0; b[i]; ++i) { h ^= (uint8_t)b[i]; h *= 1099511628211ull; }
    return h;
}

static void hash_hex(uint64_t h, char out[17]) {
    static const char *d = "0123456789abcdef";
    for (int i = 0; i < 16; ++i) { out[15 - i] = d[(int)(h & 0xFu)]; h >>= 4u; }
    out[16] = '\0';
}

static int rd_file(const char *path, char *buf, uint64_t cap, uint64_t *len) {
    long r = sys_fs_read_file(path, buf, cap - 1, len);
    if (r == 0) { buf[*len] = '\0'; return 0; }
    buf[0] = '\0'; *len = 0; return -1;
}

static int wr_file(const char *path, const char *buf, uint64_t len) {
    if (sys_fs_exists(path) > 0) return (int)sys_fs_write_file(path, buf, len);
    char parent[256], name[64];
    const char *s = strrchr(path, '/');
    if (!s) return -1;
    size_t plen = (size_t)(s - path);
    if (plen >= sizeof(parent)) return -1;
    memcpy(parent, path, plen); parent[plen] = '\0';
    strncpy(name, s + 1, sizeof(name) - 1); name[sizeof(name) - 1] = '\0';
    return (int)sys_fs_create_file(parent, name, buf, len);
}

static int exists_in(const char *buf, const char *name) {
    const char *p = buf;
    while (*p) {
        const char *line = p;
        while (*p && *p != '\n') p++;
        size_t llen = (size_t)(p - line);
        if (llen > 0 && *p == '\n') p++;
        if (llen == 0) continue;
        const char *colon = (const char*)memchr(line, ':', llen);
        if (!colon) continue;
        size_t nlen = (size_t)(colon - line);
        if (strlen(name) == nlen && memcmp(line, name, nlen) == 0) return 1;
    }
    return 0;
}

static int remove_line(char *buf, const char *name) {
    char *p = buf, *out = buf;
    int found = 0;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        int is_last = (*p == '\0');
        if (*p == '\n') *p++ = '\0';
        if (*line == '\0') continue;
        const char *colon = strchr(line, ':');
        if (colon && (size_t)(colon - line) == strlen(name) && memcmp(line, name, colon - line) == 0) {
            found = 1; continue;
        }
        size_t llen = strlen(line);
        memcpy(out, line, llen); out += llen;
        if (!is_last) *out++ = '\n';
    }
    *out = '\0';
    return found;
}

static void read_pass(char *buf, int cap) {
    int pos = 0;
    for (;;) {
        long c = sys_getchar();
        if (c < 0) continue;
        if (c == '\n' || c == '\r') break;
        if (c == 127 || c == 8) { if (pos > 0) pos--; }
        else if (c >= 32 && c < 127 && pos < cap - 1) buf[pos++] = (char)c;
    }
    buf[pos] = '\0';
}

static void print_u32(uint32_t v) {
    char buf[16];
    int p = 0;
    if (v == 0) { sys_write("0", 1); return; }
    while (v > 0 && p < 16) { buf[p++] = (char)('0' + v % 10u); v /= 10u; }
    while (p > 0) sys_write((const char*)(buf + --p), 1);
}

static int name_to_uid(const char *name, uint32_t *uid) {
    char buf[FILE_BUF];
    uint64_t len = 0;
    if (rd_file("/etc/passwd", buf, sizeof(buf), &len) != 0) return -1;
    char *p = buf;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') *p++ = '\0';
        if (!*line) continue;
        char *c1 = strchr(line, ':');
        if (!c1) continue;
        *c1++ = '\0';
        if (strcmp(line, name) == 0) { *uid = (uint32_t)atoi(c1); return 0; }
    }
    return -1;
}

static int name_to_gid(const char *name, uint32_t *gid) {
    char buf[FILE_BUF];
    uint64_t len = 0;
    if (rd_file("/etc/group", buf, sizeof(buf), &len) != 0) return -1;
    char *p = buf;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') *p++ = '\0';
        if (!*line) continue;
        char *c1 = strchr(line, ':');
        if (!c1) continue;
        *c1++ = '\0';
        if (strcmp(line, name) == 0) {
            char *c2 = strchr(c1, ':');
            if (!c2) continue;
            *c2++ = '\0';
            *gid = (uint32_t)atoi(c1);
            return 0;
        }
    }
    return -1;
}

static int count_lines(const char *buf) {
    int n = 0;
    for (const char *p = buf; *p; p++) if (*p == '\n') n++;
    return n;
}

/* -------- applet: useradd -------- */
int useradd_main(int argc, char **argv) {
    if (sys_get_uid() != 0) { sys_write("useradd: must be root\n", 22); return 1; }
    uint32_t uid = 1001, gid = 1001;
    const char *home = NULL, *shell = "/bin/sh", *username = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (argv[i][1] == 'u' && i + 1 < argc) uid = (uint32_t)atoi(argv[++i]);
            else if (argv[i][1] == 'g' && i + 1 < argc) gid = (uint32_t)atoi(argv[++i]);
            else if (argv[i][1] == 'd' && i + 1 < argc) home = argv[++i];
            else if (argv[i][1] == 's' && i + 1 < argc) shell = argv[++i];
            else { sys_write("useradd: unknown option\n", 24); return 1; }
        } else { username = argv[i]; }
    }
    if (!username) { sys_write("useradd: missing username\n", 26); return 1; }
    char homebuf[256];
    if (!home) { snprintf(homebuf, sizeof(homebuf), "/home/%s", username); home = homebuf; }
    char passwd_buf[FILE_BUF], shadow_buf[FILE_BUF], group_buf[FILE_BUF];
    uint64_t pw_len = 0, sh_len = 0, gr_len = 0;
    rd_file("/etc/passwd", passwd_buf, sizeof(passwd_buf), &pw_len);
    rd_file("/etc/shadow", shadow_buf, sizeof(shadow_buf), &sh_len);
    rd_file("/etc/group", group_buf, sizeof(group_buf), &gr_len);
    if (exists_in(passwd_buf, username)) { sys_write("useradd: user exists\n", 21); return 1; }
    uint64_t pw_off = pw_len, sh_off = sh_len, gr_off = gr_len;
    char line[512]; int n;
    n = snprintf(line, sizeof(line), "%s:%u:%u:%s:%s\n", username, uid, gid, home, shell);
    if (pw_off + (uint64_t)n + 1 < sizeof(passwd_buf)) { memcpy(passwd_buf + pw_off, line, (size_t)n + 1); pw_off += (uint64_t)n; }
    char hex[17];
    hash_hex(fnv1a64(username, "1234"), hex);
    n = snprintf(line, sizeof(line), "%s:%s\n", username, hex);
    if (sh_off + (uint64_t)n + 1 < sizeof(shadow_buf)) { memcpy(shadow_buf + sh_off, line, (size_t)n + 1); sh_off += (uint64_t)n; }
    if (!exists_in(group_buf, username)) {
        n = snprintf(line, sizeof(line), "%s:%u:\n", username, gid);
        if (gr_off + (uint64_t)n + 1 < sizeof(group_buf)) { memcpy(group_buf + gr_off, line, (size_t)n + 1); gr_off += (uint64_t)n; }
    }
    wr_file("/etc/passwd", passwd_buf, pw_off);
    wr_file("/etc/shadow", shadow_buf, sh_off);
    wr_file("/etc/group", group_buf, gr_off);
    sys_write("useradd: added user ", 19); sys_write(username, strlen(username)); sys_write("\n", 1);
    return 0;
}

/* -------- applet: userdel -------- */
int userdel_main(int argc, char **argv) {
    if (sys_get_uid() != 0) { sys_write("userdel: must be root\n", 22); return 1; }
    if (argc < 2) { sys_write("userdel: missing username\n", 26); return 1; }
    const char *username = argv[1];
    char passwd_buf[FILE_BUF], shadow_buf[FILE_BUF];
    uint64_t pw_len = 0, sh_len = 0;
    if (rd_file("/etc/passwd", passwd_buf, sizeof(passwd_buf), &pw_len) != 0) { sys_write("userdel: cannot read /etc/passwd\n", 33); return 1; }
    rd_file("/etc/shadow", shadow_buf, sizeof(shadow_buf), &sh_len);
    if (!remove_line(passwd_buf, username)) { sys_write("userdel: user not found\n", 24); return 1; }
    remove_line(shadow_buf, username);
    wr_file("/etc/passwd", passwd_buf, strlen(passwd_buf));
    wr_file("/etc/shadow", shadow_buf, strlen(shadow_buf));
    sys_write("userdel: removed user ", 22); sys_write(username, strlen(username)); sys_write("\n", 1);
    return 0;
}

/* -------- applet: passwd -------- */
static int replace_shadow_line(char *buf, const char *name, const char *new_hash) {
    char *p = buf;
    char tmp[FILE_BUF];
    int pos = 0, found = 0;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        int is_last = (*p == '\0');
        if (*p == '\n') *p++ = '\0';
        if (*line) {
            const char *colon = strchr(line, ':');
            if (colon && (size_t)(colon - line) == strlen(name) && memcmp(line, name, colon - line) == 0) {
                int n = snprintf(tmp + pos, sizeof(tmp) - (size_t)pos, "%s:%s\n", name, new_hash);
                if (n > 0) pos += n;
                found = 1;
            } else {
                size_t llen = strlen(line);
                if (pos + (int)llen + 1 < (int)sizeof(tmp)) { memcpy(tmp + pos, line, llen); pos += (int)llen; if (!is_last) tmp[pos++] = '\n'; }
            }
        } else if (!is_last) { if (pos + 1 < (int)sizeof(tmp)) tmp[pos++] = '\n'; }
    }
    tmp[pos] = '\0';
    memcpy(buf, tmp, (size_t)pos + 1);
    return found;
}

int passwd_main(int argc, char **argv) {
    const char *username = NULL;
    uint32_t uid = sys_get_uid();
    if (argc >= 2) { username = argv[1]; }
    else {
        char pwdbuf[FILE_BUF]; uint64_t len = 0;
        if (rd_file("/etc/passwd", pwdbuf, sizeof(pwdbuf), &len) == 0) {
            char *p = pwdbuf;
            while (*p) {
                char *line = p;
                while (*p && *p != '\n') p++;
                if (*p == '\n') *p++ = '\0';
                if (!*line) continue;
                char *c1 = strchr(line, ':');
                if (!c1) continue;
                *c1++ = '\0';
                if ((uint32_t)atoi(c1) == uid) { username = line; break; }
            }
        }
        if (!username) { sys_write("passwd: cannot determine user\n", 31); return 1; }
    }
    if (uid == 0) {
        sys_write("passwd: new password: ", 22);
        char new1[64]; read_pass(new1, sizeof(new1)); sys_write("\n", 1);
        sys_write("passwd: confirm: ", 18);
        char new2[64]; read_pass(new2, sizeof(new2)); sys_write("\n", 1);
        if (strcmp(new1, new2) != 0) { sys_write("passwd: passwords do not match\n", 32); return 1; }
        char hex[17]; hash_hex(fnv1a64(username, new1), hex);
        char shadow_buf[FILE_BUF]; uint64_t sh_len = 0;
        if (rd_file("/etc/shadow", shadow_buf, sizeof(shadow_buf), &sh_len) != 0) { sys_write("passwd: cannot read /etc/shadow\n", 33); return 1; }
        if (!replace_shadow_line(shadow_buf, username, hex)) { sys_write("passwd: user not found\n", 23); return 1; }
        wr_file("/etc/shadow", shadow_buf, strlen(shadow_buf));
        sys_write("passwd: password updated\n", 25);
    } else {
        sys_write("passwd: old password: ", 22);
        char old[64]; read_pass(old, sizeof(old)); sys_write("\n", 1);
        if (sys_auth_user(username, old) != 0) { sys_write("passwd: authentication failed\n", 31); return 1; }
        sys_write("passwd: new password: ", 22);
        char new1[64]; read_pass(new1, sizeof(new1)); sys_write("\n", 1);
        sys_write("passwd: confirm: ", 18);
        char new2[64]; read_pass(new2, sizeof(new2)); sys_write("\n", 1);
        if (strcmp(new1, new2) != 0) { sys_write("passwd: passwords do not match\n", 32); return 1; }
        char hex[17]; hash_hex(fnv1a64(username, new1), hex);
        char shadow_buf[FILE_BUF]; uint64_t sh_len = 0;
        if (rd_file("/etc/shadow", shadow_buf, sizeof(shadow_buf), &sh_len) != 0) { sys_write("passwd: cannot read /etc/shadow\n", 33); return 1; }
        if (!replace_shadow_line(shadow_buf, username, hex)) { sys_write("passwd: user not found\n", 23); return 1; }
        wr_file("/etc/shadow", shadow_buf, strlen(shadow_buf));
        sys_write("passwd: password updated\n", 25);
    }
    return 0;
}

/* -------- applet: id -------- */
int id_main(int argc, char **argv) {
    const char *username = NULL;
    if (argc >= 2) username = argv[1];
    uint32_t uid = 0, gid = 0;
    char name[64] = "";
    if (username) {
        char buf[FILE_BUF]; uint64_t len = 0;
        if (rd_file("/etc/passwd", buf, sizeof(buf), &len) != 0) { sys_write("id: cannot read /etc/passwd\n", 28); return 1; }
        char *p = buf; int found = 0;
        while (*p && !found) {
            char *line = p;
            while (*p && *p != '\n') p++;
            if (*p == '\n') *p++ = '\0';
            if (!*line) continue;
            char *c1 = strchr(line, ':');
            if (!c1 || (size_t)(c1 - line) != strlen(username) || memcmp(line, username, c1 - line) != 0) continue;
            *c1++ = '\0'; strncpy(name, line, sizeof(name) - 1);
            uid = (uint32_t)atoi(c1);
            char *c2 = strchr(c1, ':'); if (c2) gid = (uint32_t)atoi(c2 + 1);
            found = 1;
        }
        if (!found) { sys_write("id: unknown user\n", 17); return 1; }
    } else {
        uid = sys_get_uid(); gid = sys_getgid();
        char buf[FILE_BUF]; uint64_t len = 0;
        if (rd_file("/etc/passwd", buf, sizeof(buf), &len) == 0) {
            char *p = buf;
            while (*p) {
                char *line = p;
                while (*p && *p != '\n') p++;
                if (*p == '\n') *p++ = '\0';
                if (!*line) continue;
                char *c1 = strchr(line, ':');
                if (!c1) continue;
                *c1++ = '\0';
                if ((uint32_t)atoi(c1) == uid) { strncpy(name, line, sizeof(name) - 1); break; }
            }
        }
        if (!name[0]) { sys_write("id: cannot find username\n", 25); return 1; }
    }
    sys_write("uid=", 4); print_u32(uid); sys_write("(", 1); sys_write(name, strlen(name)); sys_write(") gid=", 6);
    print_u32(gid); sys_write("(", 1); sys_write(name, strlen(name)); sys_write(")", 1);
    char group_buf[FILE_BUF]; uint64_t gr_len = 0;
    if (rd_file("/etc/group", group_buf, sizeof(group_buf), &gr_len) == 0) {
        sys_write(" groups=", 8);
        int first = 1;
        char *p = group_buf;
        while (*p) {
            char *line = p;
            while (*p && *p != '\n') p++;
            if (*p == '\n') *p++ = '\0';
            if (!*line) continue;
            char *c1 = strchr(line, ':');
            if (!c1) continue;
            *c1++ = '\0'; char *c2 = strchr(c1, ':');
            if (!c2) continue;
            *c2++ = '\0';
            uint32_t gr_gid = (uint32_t)atoi(c1);
            char *users = c2;
            int in = (gr_gid == gid) ? 1 : 0;
            if (!in) {
                char *u = users;
                while (*u) {
                    while (*u == ',') u++;
                    if (!*u) break;
                    char *end = u; while (*end && *end != ',') end++;
                    if ((size_t)(end - u) == strlen(name) && memcmp(u, name, end - u) == 0) { in = 1; break; }
                    u = end;
                }
            }
            if (in) {
                if (!first) sys_write(",", 1);
                first = 0; print_u32(gr_gid); sys_write("(", 1); sys_write(line, strlen(line)); sys_write(")", 1);
            }
        }
    }
    sys_write("\n", 1);
    return 0;
}

/* -------- applet: whoami -------- */
int whoami_main(int argc, char **argv) {
    (void)argc; (void)argv;
    uint32_t uid = sys_get_uid();
    char buf[FILE_BUF]; uint64_t len = 0;
    if (sys_fs_read_file("/etc/passwd", buf, sizeof(buf) - 1, &len) != 0) { sys_write("whoami: cannot read /etc/passwd\n", 32); return 1; }
    buf[len] = '\0';
    char *p = buf;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') *p++ = '\0';
        if (!*line) continue;
        char *c1 = strchr(line, ':');
        if (!c1) continue;
        *c1++ = '\0';
        if ((uint32_t)atoi(c1) == uid) { sys_write(line, strlen(line)); sys_write("\n", 1); return 0; }
    }
    sys_write("whoami: cannot determine username\n", 35);
    return 1;
}

/* -------- applet: groups -------- */
int groups_main(int argc, char **argv) {
    const char *username = NULL;
    if (argc >= 2) username = argv[1];
    if (!username) {
        uint32_t uid = sys_get_uid();
        char buf[FILE_BUF]; uint64_t len = 0;
        if (rd_file("/etc/passwd", buf, sizeof(buf), &len) != 0) { sys_write("groups: cannot read /etc/passwd\n", 32); return 1; }
        char *p = buf;
        while (*p) {
            char *line = p;
            while (*p && *p != '\n') p++;
            if (*p == '\n') *p++ = '\0';
            if (!*line) continue;
            char *c1 = strchr(line, ':');
            if (!c1) continue;
            *c1++ = '\0';
            if ((uint32_t)atoi(c1) == uid) { username = line; break; }
        }
        if (!username) { sys_write("groups: unknown user\n", 21); return 1; }
    }
    char buf[FILE_BUF]; uint64_t len = 0;
    if (rd_file("/etc/group", buf, sizeof(buf), &len) != 0) { sys_write("groups: cannot read /etc/group\n", 31); return 1; }
    sys_write(username, strlen(username)); sys_write(" : ", 3);
    int first = 1;
    char *p = buf;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') *p++ = '\0';
        if (!*line) continue;
        char *c1 = strchr(line, ':');
        if (!c1) continue;
        *c1++ = '\0'; char *c2 = strchr(c1, ':');
        if (!c2) continue;
        *c2++ = '\0'; char *users = c2;
        int in = 0;
        if (strlen(users) == 0 && strcmp(line, username) == 0) in = 1;
        else {
            char *u = users;
            while (*u) {
                while (*u == ',') u++;
                if (!*u) break;
                char *end = u; while (*end && *end != ',') end++;
                if ((size_t)(end - u) == strlen(username) && memcmp(u, username, end - u) == 0) { in = 1; break; }
                u = end;
            }
        }
        if (in) {
            if (!first) sys_write(" ", 1);
            first = 0; sys_write(line, strlen(line));
        }
    }
    sys_write("\n", 1);
    return 0;
}

/* -------- applet: chmod -------- */
int chmod_main(int argc, char **argv) {
    if (argc < 3) { sys_write("chmod: usage: chmod mode path\n", 30); return 1; }
    const char *s = argv[1];
    int mode = 0;
    while (*s >= '0' && *s <= '7') { mode = (mode << 3) | (*s++ - '0'); }
    if (*s != '\0') { sys_write("chmod: invalid mode\n", 20); return 1; }
    if (sys_chmod(argv[2], (uint32_t)mode) != 0) { sys_write("chmod: failed\n", 15); return 1; }
    return 0;
}

/* -------- applet: chown -------- */
int chown_main(int argc, char **argv) {
    if (argc < 3) { sys_write("chown: usage: chown owner:group path\n", 36); return 1; }
    const char *spec = argv[1], *path = argv[2];
    uint32_t owner = 0, group = 0;
    const char *colon = strchr(spec, ':');
    if (colon) {
        char owner_buf[64];
        size_t olen = (size_t)(colon - spec);
        if (olen > 0) {
            if (olen >= sizeof(owner_buf)) { sys_write("chown: owner too long\n", 22); return 1; }
            memcpy(owner_buf, spec, olen); owner_buf[olen] = '\0';
            if (name_to_uid(owner_buf, &owner) != 0) owner = (uint32_t)atoi(owner_buf);
        }
        const char *gname = colon + 1;
        if (*gname) {
            if (name_to_gid(gname, &group) != 0) group = (uint32_t)atoi(gname);
        }
    } else {
        if (name_to_uid(spec, &owner) != 0) owner = (uint32_t)atoi(spec);
    }
    if (sys_chown(path, owner, group) != 0) { sys_write("chown: failed\n", 15); return 1; }
    return 0;
}

/* -------- applet: su -------- */
int su_main(int argc, char **argv) {
    const char *username = "root";
    if (argc >= 2) username = argv[1];
    sys_write("su: password: ", 14);
    char pass[64]; read_pass(pass, sizeof(pass)); sys_write("\n", 1);
    if (sys_auth_user(username, pass) != 0) { sys_write("su: authentication failed\n", 26); return 1; }
    sys_write("su: authenticated\n", 18);
    return 0;
}

/* -------- applet: sudo -------- */
int sudo_main(int argc, char **argv) {
    (void)argc; (void)argv;
    if (sys_get_uid() == 0) { sys_write("sudo: already root\n", 19); return 0; }
    sys_write("sudo: password for root: ", 25);
    char pass[64]; read_pass(pass, sizeof(pass)); sys_write("\n", 1);
    if (sys_auth_user("root", pass) != 0) { sys_write("sudo: authentication failed\n", 28); return 1; }
    sys_set_uid(0);
    sys_write("sudo: authenticated as root\n", 28);
    return 0;
}

/* -------- applet: addgroup -------- */
int addgroup_main(int argc, char **argv) {
    if (sys_get_uid() != 0) { sys_write("addgroup: must be root\n", 23); return 1; }
    uint32_t gid = 0;
    const char *groupname = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (argv[i][1] == 'g' && i + 1 < argc) gid = (uint32_t)atoi(argv[++i]);
            else { sys_write("addgroup: unknown option\n", 25); return 1; }
        } else { groupname = argv[i]; }
    }
    if (!groupname) { sys_write("addgroup: missing group name\n", 29); return 1; }
    char buf[FILE_BUF]; uint64_t len = 0;
    rd_file("/etc/group", buf, sizeof(buf), &len);
    if (exists_in(buf, groupname)) { sys_write("addgroup: group exists\n", 23); return 1; }
    if (gid == 0) gid = 1000 + (uint32_t)count_lines(buf) + 1;
    uint64_t off = len;
    char line[256];
    int n = snprintf(line, sizeof(line), "%s:%u:\n", groupname, gid);
    if (off + (uint64_t)n + 1 < sizeof(buf)) { memcpy(buf + off, line, (size_t)n + 1); off += (uint64_t)n; }
    wr_file("/etc/group", buf, off);
    sys_write("addgroup: added group ", 22); sys_write(groupname, strlen(groupname)); sys_write("\n", 1);
    return 0;
}

/* -------- applet: delgroup -------- */
int delgroup_main(int argc, char **argv) {
    if (sys_get_uid() != 0) { sys_write("delgroup: must be root\n", 23); return 1; }
    if (argc < 2) { sys_write("delgroup: missing group name\n", 29); return 1; }
    const char *groupname = argv[1];
    char buf[FILE_BUF]; uint64_t len = 0;
    if (rd_file("/etc/group", buf, sizeof(buf), &len) != 0) { sys_write("delgroup: cannot read /etc/group\n", 33); return 1; }
    if (!remove_line(buf, groupname)) { sys_write("delgroup: group not found\n", 26); return 1; }
    wr_file("/etc/group", buf, strlen(buf));
    sys_write("delgroup: removed group ", 24); sys_write(groupname, strlen(groupname)); sys_write("\n", 1);
    return 0;
}

/* -------- busybox dispatcher -------- */
typedef struct {
    const char *name;
    int (*func)(int, char **);
} applet_t;

static const applet_t applets[] = {
    {"useradd", useradd_main},
    {"userdel", userdel_main},
    {"passwd", passwd_main},
    {"id", id_main},
    {"whoami", whoami_main},
    {"groups", groups_main},
    {"chmod", chmod_main},
    {"chown", chown_main},
    {"su", su_main},
    {"sudo", sudo_main},
    {"addgroup", addgroup_main},
    {"delgroup", delgroup_main},
    {NULL, NULL}
};

int busybox_main(int argc, char **argv) {
    (void)argc; (void)argv;
    sys_write("Busybox v1.0 (NTux-OS)\n", 24);
    sys_write("Usage: busybox <applet> [args]\n\nApplets:\n", 40);
    for (int i = 0; applets[i].name; ++i) {
        sys_write("  ", 2);
        sys_write(applets[i].name, strlen(applets[i].name));
        sys_write("\n", 1);
    }
    return 0;
}

void ntux_user_entry(void) {
    int argc = ntux_argc();
    char **argv = ntux_argv();

    const char *name = argv[0];
    const char *slash = strrchr(name, '/');
    if (slash) name = slash + 1;

    if (strcmp(name, "busybox") == 0) {
        if (argc < 2) {
            busybox_main(argc, argv);
            sys_exit(0);
        }
        name = argv[1];
        argc--;
        argv++;
    }

    for (int i = 0; applets[i].name; ++i) {
        if (strcmp(name, applets[i].name) == 0) {
            int rc = applets[i].func(argc, argv);
            sys_exit(rc);
        }
    }

    sys_write("busybox: applet not found: ", 27);
    sys_write(name, strlen(name));
    sys_write("\n", 1);
    sys_exit(1);
}
