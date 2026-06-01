#ifndef NTUX_ACPI_H
#define NTUX_ACPI_H

#include <stdint.h>
#include <stdbool.h>

/* ACPI PM1 control defaults (legacy fallback). */
#define PM1_CNT_ADDR               0x1C14
#define PM1_CNT_SCI_EN             0x0001
#define PM1_CNT_SLEEP_TYPE_MASK    0x1C00
#define PM1_CNT_SLEEP_TYPE_SHIFT   10
#define PM1_CNT_SLEEP_ENABLE       0x2000

#define PM1_SLEEP_TYPE_S0          0x00
#define PM1_SLEEP_TYPE_S1          0x01
#define PM1_SLEEP_TYPE_S3          0x03
#define PM1_SLEEP_TYPE_S4          0x04
#define PM1_SLEEP_TYPE_S5          0x05

struct acpi_sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

bool acpi_init(void);
bool acpi_find_smm(void);
void acpi_enable_sci(void);
void acpi_enter_sleep(uint8_t sleep_state);
bool acpi_try_reset(void);

const struct acpi_sdt_header* acpi_find_table(const char* sig4);

#endif
