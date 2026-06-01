#ifndef NTUX_PARTUTIL_APP_H
#define NTUX_PARTUTIL_APP_H

#include <stdint.h>
#include <window.h>
#include <syscall.h>

#define PARTUTIL_WIN_W 900
#define PARTUTIL_WIN_H 520

#define PARTUTIL_MAX_DRIVES 16
#define PARTUTIL_MAX_PARTS 16

typedef enum {
    PARTUTIL_ACT_NONE = 0,
    PARTUTIL_ACT_RESCAN,
    PARTUTIL_ACT_WIPE,
    PARTUTIL_ACT_GPT_SINGLE,
    PARTUTIL_ACT_GPT_DUAL,
    PARTUTIL_ACT_MBR_SINGLE,
    PARTUTIL_ACT_MKFS_FAT,
    PARTUTIL_ACT_MKFS_EXT2,
    PARTUTIL_ACT_MKFS_EXT4
} partutil_action_t;

typedef struct {
    ntux_block_device_info_t drives[PARTUTIL_MAX_DRIVES];
    uint64_t drive_ids[PARTUTIL_MAX_DRIVES];
    uint64_t drive_count;

    ntux_partition_info_t parts[PARTUTIL_MAX_PARTS];
    uint64_t part_count;

    int sel_drive;
    int sel_part;
    int focus_group;
    int action_focus;

    char status[128];
    uint64_t status_tick;

    int confirm_active;
    int confirm_choice;
    partutil_action_t confirm_action;
} partutil_state_t;

void partutil_init(partutil_state_t* st);
void partutil_rescan(partutil_state_t* st);
void partutil_set_status(partutil_state_t* st, const char* msg);

void partutil_draw(window_t id, const partutil_state_t* st);
partutil_action_t partutil_hit_test(const partutil_state_t* st, int mx, int my);

int partutil_do_action(partutil_state_t* st, partutil_action_t act);

#endif
