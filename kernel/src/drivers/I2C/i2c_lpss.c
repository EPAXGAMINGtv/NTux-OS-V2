// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.

#include "i2c_lpss.h"
#include "../core/kutils.h"

#define MAX_LPSS_I2C_CONTROLLERS 8

static i2c_lpss_controller_t controllers[MAX_LPSS_I2C_CONTROLLERS];
static int controller_count = 0;

int i2c_lpss_init(void) {
    serial_write("[I2C-LPSS] Port not implemented, skipping\n");
    return 0;
}

int i2c_lpss_get_count(void) {
    return controller_count;
}

i2c_lpss_controller_t* i2c_lpss_get(int index) {
    if (index < 0 || index >= controller_count) return NULL;
    return &controllers[index];
}

i2c_lpss_controller_t* i2c_lpss_get_by_base(uint64_t base_phys) {
    (void)base_phys;
    return NULL;
}

const aml_i2c_dev_t* i2c_lpss_get_acpi_device(i2c_lpss_controller_t *ctrl) {
    (void)ctrl;
    return NULL;
}
