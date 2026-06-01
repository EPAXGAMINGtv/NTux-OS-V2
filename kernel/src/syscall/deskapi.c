#include <syscall/deskapi.h>
#include <sched/thread.h>
#include <lib/string.h>

typedef struct {
    uint8_t has;
    uint32_t code;
    char text[DIALOG_MAX_TEXT];
} dialog_slot_t;

static dialog_slot_t g_dialog_slots[MAX_THREADS] = {0};

typedef struct {
    uint16_t len;
    char data[DESKAPI_MAX_MSG];
} deskapi_msg_t;

static deskapi_msg_t g_deskapi_ring[DESKAPI_RING_SIZE];
static uint32_t g_deskapi_head = 0;
static uint32_t g_deskapi_tail = 0;
static uint32_t g_deskapi_count = 0;
static volatile uint8_t g_deskapi_lock = 0;

static void deskapi_lock(void) {
    while (__atomic_test_and_set(&g_deskapi_lock, __ATOMIC_ACQUIRE)) {
        __asm__ volatile("pause");
    }
}

static void deskapi_unlock(void) {
    __atomic_clear(&g_deskapi_lock, __ATOMIC_RELEASE);
}

int deskapi_push(const char* buf, uint64_t len) {
    if (!buf || len == 0 || len > DESKAPI_MAX_MSG) return -1;
    deskapi_lock();
    if (g_deskapi_count >= DESKAPI_RING_SIZE) {
        deskapi_unlock();
        return -2;
    }
    deskapi_msg_t* msg = &g_deskapi_ring[g_deskapi_tail];
    memcpy(msg->data, buf, (size_t)len);
    msg->len = (uint16_t)len;
    g_deskapi_tail = (g_deskapi_tail + 1u) % DESKAPI_RING_SIZE;
    g_deskapi_count++;
    deskapi_unlock();
    return 0;
}

int deskapi_pop(char* out, uint64_t cap, uint64_t* out_len) {
    if (!out || cap == 0 || cap > (DESKAPI_MAX_MSG + 1u)) return -1;
    deskapi_lock();
    if (g_deskapi_count == 0) {
        deskapi_unlock();
        if (out_len) *out_len = 0;
        return 1;
    }
    deskapi_msg_t* msg = &g_deskapi_ring[g_deskapi_head];
    uint64_t len = msg->len;
    if (cap <= len) {
        deskapi_unlock();
        return -2;
    }
    memcpy(out, msg->data, (size_t)len);
    out[len] = '\0';
    if (out_len) *out_len = len;
    g_deskapi_head = (g_deskapi_head + 1u) % DESKAPI_RING_SIZE;
    g_deskapi_count--;
    deskapi_unlock();
    return 0;
}

int deskapi_dialog_push(int tid, uint32_t code, const char* text) {
    if (tid < 0 || tid >= MAX_THREADS || !text) return -1;
    dialog_slot_t* slot = &g_dialog_slots[tid];
    slot->has = 1;
    slot->code = code;
    size_t len = strlen(text);
    if (len >= DIALOG_MAX_TEXT) len = DIALOG_MAX_TEXT - 1;
    memcpy(slot->text, text, len);
    slot->text[len] = '\0';
    return 0;
}

int deskapi_dialog_pop(int tid, char* out, uint64_t cap, uint32_t* out_code) {
    if (tid < 0 || tid >= MAX_THREADS || !out || cap == 0 || cap > (uint64_t)DIALOG_MAX_TEXT) return -1;
    dialog_slot_t* slot = &g_dialog_slots[tid];
    if (!slot->has) {
        if (out_code) *out_code = 0;
        return 1;
    }
    size_t len = strlen(slot->text);
    if (len + 1 > cap) len = cap - 1;
    memcpy(out, slot->text, len);
    out[len] = '\0';
    if (out_code) *out_code = slot->code;
    slot->has = 0;
    slot->code = 0;
    slot->text[0] = '\0';
    return 0;
}
