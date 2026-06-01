#include "hhdm.h"
#include <limine.h>

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request g_hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

uint64_t hhdm_offset_get(void) {
    if (!g_hhdm_request.response) return 0;
    return g_hhdm_request.response->offset;
}

int hhdm_ready(void) {
    return g_hhdm_request.response != 0;
}
