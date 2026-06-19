#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <syscall.h>
#include <args.h>

static uint64_t parse_u64(const char* s) {
    uint64_t v = 0;
    if (!s) return 0;
    for (size_t i = 0; s[i]; ++i) {
        if (s[i] < '0' || s[i] > '9') return 0;
        v = v * 10u + (uint64_t)(s[i] - '0');
    }
    return v;
}

void ntux_user_entry(void) {
    const char* fs = ntux_arg(0);
    const char* drive_s = ntux_arg(1);
    const char* lba_s = ntux_arg(2);
    const char* sectors_s = ntux_arg(3);
    if (!fs || !drive_s || !lba_s || !sectors_s) {
        puts("usage: format <ext2|ext4|fat> <drive> <lba_start> <sectors>");
        sys_exit(2);
    }
    uint64_t drive = parse_u64(drive_s);
    uint64_t lba = parse_u64(lba_s);
    uint64_t sectors = parse_u64(sectors_s);
    long rc = -1;
    if (strcmp(fs, "ext2") == 0) rc = sys_mkfs_ext2(drive, lba, sectors);
    else if (strcmp(fs, "ext4") == 0) rc = sys_mkfs_ext4(drive, lba, sectors);
    else if (strcmp(fs, "fat") == 0 || strcmp(fs, "fat32") == 0) rc = sys_mkfs_fat(drive, lba, sectors, 0);
    else {
        puts("format: unknown fs");
        sys_exit(2);
    }
    sys_exit(rc == 0 ? 0 : 1);
}
