#ifndef PS2_H
#define PS2_H

#include <stdint.h>
#include <stdbool.h>

/* PS/2 Controller Ports */
#define PS2_DATA    0x60
#define PS2_STATUS  0x64
#define PS2_COMMAND 0x64

/* PS/2 Controller Commands */
#define PS2_CMD_READ_CONFIG      0x20
#define PS2_CMD_WRITE_CONFIG     0x60
#define PS2_CMD_DISABLE_PORT2    0xA7
#define PS2_CMD_ENABLE_PORT2     0xA8
#define PS2_CMD_TEST_PORT2       0xA9
#define PS2_CMD_TEST_CONTROLLER  0xAA
#define PS2_CMD_TEST_PORT1       0xAB
#define PS2_CMD_DISABLE_PORT1    0xAD
#define PS2_CMD_ENABLE_PORT1     0xAE
#define PS2_CMD_WRITE_PORT2_IN   0xD4

/* PS/2 Device Commands */
#define PS2_DEV_SET_DEFAULTS     0xF6
#define PS2_DEV_ENABLE_SCANNING  0xF4
#define PS2_DEV_DISABLE_SCANNING 0xF5
#define PS2_DEV_IDENTIFY         0xF2
#define PS2_DEV_RESET            0xFF

/* PS/2 Status Register Bits */
#define PS2_STATUS_OUTPUT_FULL   0x01
#define PS2_STATUS_INPUT_FULL    0x02
#define PS2_STATUS_SYSTEM        0x04
#define PS2_STATUS_CMD_DATA      0x08
#define PS2_STATUS_KEYLOCK       0x10
#define PS2_STATUS_AUX_OUTPUT    0x20
#define PS2_STATUS_TIMEOUT       0x40
#define PS2_STATUS_PARITY        0x80

/* PS/2 Config Byte Bits */
#define PS2_CFG_PORT1_INT        0x01
#define PS2_CFG_PORT2_INT        0x02
#define PS2_CFG_SYSTEM           0x04
#define PS2_CFG_PORT1_DISABLED   0x10
#define PS2_CFG_PORT2_DISABLED   0x20
#define PS2_CFG_PORT1_TRANSLATE  0x40

/* Function Declarations */
bool ps2_init(void);
void ps2_wait_write(void);
void ps2_wait_read(void);
void ps2_flush(void);

uint8_t ps2_read_data(void);
void ps2_write_data(uint8_t data);
void ps2_write_command(uint8_t cmd);

bool ps2_write_device(uint8_t port, uint8_t data);
uint8_t ps2_read_config(void);
void ps2_write_config(uint8_t cfg);

bool ps2_is_dual_channel(void);

#endif
