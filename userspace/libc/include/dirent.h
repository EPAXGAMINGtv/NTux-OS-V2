#ifndef USER_DIRENT_H
#define USER_DIRENT_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DIR DIR;

struct dirent {
    char d_name[256];
    unsigned char d_type;
};

#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8

DIR *opendir(const char *path);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);

#ifdef __cplusplus
}
#endif

#endif
