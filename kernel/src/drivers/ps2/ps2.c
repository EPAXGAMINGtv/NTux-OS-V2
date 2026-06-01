#include "ps2.h"
#include <arch/x86_64/io.h>
#include <drivers/framebuffer/kprint.h>

static bool dual_channel = false;

static inline uint8_t ps2_read_status(void) {
    return inb(PS2_STATUS);
}

void ps2_wait_write(void) {
    for (int i = 0; i < 100000; ++i) {
        if ((ps2_read_status() & PS2_STATUS_INPUT_FULL) == 0) return;
        __asm__ volatile("pause");
    }
}

void ps2_wait_read(void) {
    for (int i = 0; i < 100000; ++i) {
        if ((ps2_read_status() & PS2_STATUS_OUTPUT_FULL) != 0) return;
        __asm__ volatile("pause");
    }
}

void ps2_flush(void) {
    while (ps2_read_status() & PS2_STATUS_OUTPUT_FULL) {
        (void)inb(PS2_DATA);
    }
}

uint8_t ps2_read_data(void) {
    ps2_wait_read();
    return inb(PS2_DATA);
}

void ps2_write_data(uint8_t data) {
    ps2_wait_write();
    outb(PS2_DATA, data);
}

void ps2_write_command(uint8_t cmd) {
    ps2_wait_write();
    outb(PS2_COMMAND, cmd);
}

uint8_t ps2_read_config(void) {
    ps2_write_command(PS2_CMD_READ_CONFIG);
    return ps2_read_data();
}

void ps2_write_config(uint8_t cfg) {
    ps2_write_command(PS2_CMD_WRITE_CONFIG);
    ps2_write_data(cfg);
}

bool ps2_write_device(uint8_t port, uint8_t data) {
    if (port == 2) {
        ps2_write_command(PS2_CMD_WRITE_PORT2_IN);
    }
    ps2_write_data(data);
    
    /* Wait for ACK (0xFA) */
    for (int i = 0; i < 3; ++i) {
        uint8_t ack = ps2_read_data();
        if (ack == 0xFA) return true;
        if (ack != 0xFE) break; /* Not RESEND, probably error */
        /* If 0xFE, resend the data */
        if (port == 2) ps2_write_command(PS2_CMD_WRITE_PORT2_IN);
        ps2_write_data(data);
    }
    return false;
}

bool ps2_is_dual_channel(void) {
    return dual_channel;
}

bool ps2_init(void) {
    kprint("[PS2] Initializing controller...\n");
    
    /* 1. Disable Ports */
    ps2_write_command(PS2_CMD_DISABLE_PORT1);
    ps2_write_command(PS2_CMD_DISABLE_PORT2);
    
    /* 2. Flush Output Buffer */
    ps2_flush();
    
    /* 3. Set Controller Configuration Byte */
    uint8_t cfg = ps2_read_config();
    cfg &= ~(PS2_CFG_PORT1_INT | PS2_CFG_PORT2_INT | PS2_CFG_PORT1_TRANSLATE);
    ps2_write_config(cfg);
    
    /* 4. Controller Self-Test */
    ps2_write_command(PS2_CMD_TEST_CONTROLLER);
    uint8_t test_res = ps2_read_data();
    if (test_res != 0x55) {
        kprint_error("[PS2] Controller self-test failed!\n");
        return false;
    }
    
    /* Restore config in case self-test reset it */
    ps2_write_config(cfg);
    
    /* 5. Determine if Dual Channel */
    ps2_write_command(PS2_CMD_ENABLE_PORT2);
    cfg = ps2_read_config();
    if ((cfg & PS2_CFG_PORT2_DISABLED) == 0) {
        dual_channel = true;
        ps2_write_command(PS2_CMD_DISABLE_PORT2);
    } else {
        /* Some controllers keep the bit set even when a second device exists. */
        ps2_write_command(PS2_CMD_TEST_PORT2);
        uint8_t port2_test = ps2_read_data();
        dual_channel = (port2_test == 0);
        ps2_write_command(PS2_CMD_DISABLE_PORT2);
    }
    
    /* 6. Interface Tests */
    ps2_write_command(PS2_CMD_TEST_PORT1);
    if (ps2_read_data() != 0) {
        kprint_error("[PS2] Port 1 interface test failed!\n");
    }
    
    if (dual_channel) {
        ps2_write_command(PS2_CMD_TEST_PORT2);
        if (ps2_read_data() != 0) {
            kprint_error("[PS2] Port 2 interface test failed!\n");
            dual_channel = false;
        }
    }
    
    /* 7. Enable Ports */
    ps2_write_command(PS2_CMD_ENABLE_PORT1);
    if (dual_channel) {
        ps2_write_command(PS2_CMD_ENABLE_PORT2);
    }
    
    /* 8. Enable Interrupts in Config Byte */
    cfg = ps2_read_config();
    cfg |= PS2_CFG_PORT1_INT;
    if (dual_channel) {
        cfg |= PS2_CFG_PORT2_INT;
    }
    ps2_write_config(cfg);
    
    kprint_ok("[PS2] Controller initialized (Dual Channel: ");
    kprint(dual_channel ? "Yes" : "No");
    kprint(")\n");
    
    return true;
}
