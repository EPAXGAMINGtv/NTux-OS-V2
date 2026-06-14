#ifndef SYS_USER_H
#define SYS_USER_H
#include <stdint.h>
#include <stddef.h>
#define USER_MAX_NAME 32
#define USER_MAX_PASS 128
#define USER_MAX_GROUPS 32
#define USER_DB_MAX 64

typedef struct {
    char name[USER_MAX_NAME];
    uint32_t uid;
    uint32_t gid;
    char home[USER_MAX_NAME];
    char shell[USER_MAX_NAME];
    char passwd_hash[USER_MAX_PASS]; /* from /etc/shadow */
} user_entry_t;

typedef struct {
    char name[USER_MAX_NAME];
    uint32_t gid;
    uint32_t members[USER_MAX_GROUPS];
    int member_count;
} group_entry_t;

int sys_user_init(void);
int sys_user_load(void);
int sys_user_save(void);
int sys_user_add(const char* name, uint32_t uid, uint32_t gid, const char* home, const char* shell, const char* hash);
int sys_user_del(const char* name);
int sys_user_set_hash(const char* name, const char* hash);
const user_entry_t* sys_user_get_by_name(const char* name);
const user_entry_t* sys_user_get_by_uid(uint32_t uid);
int sys_user_get_count(void);
const user_entry_t* sys_user_get_all(void);

int sys_group_add(const char* name, uint32_t gid);
int sys_group_del(const char* name);
int sys_group_add_member(const char* group, uint32_t uid);
int sys_group_del_member(const char* group, uint32_t uid);
const group_entry_t* sys_group_get_by_name(const char* name);
const group_entry_t* sys_group_get_by_gid(uint32_t gid);

int sys_auth_user(const char* name, const char* password);
uint32_t sys_get_default_gid(void);
#endif
