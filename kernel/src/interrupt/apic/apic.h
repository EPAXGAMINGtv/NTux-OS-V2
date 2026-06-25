#ifndef APIC_H
#define APIC_H

#include <stdint.h>
#include <stdbool.h>

/*
 * ── Local APIC Register Offsets ──────────────────────────────────────
 * The local APIC is an MMIO device mapped at 0xFEE00000 by default.
 * Each register is 32 bits wide and accessed at a 16-byte aligned offset.
 */

#define APIC_ID                     0x020   /* Local APIC ID (read/write on BSP, read-only on APs) */
#define APIC_VERSION                0x030   /* Version register (bits 0-7 = version, 16-23 = max LVT entry) */
#define APIC_TASK_PRIORITY          0x080   /* Task Priority Register (TPR) — filters interrupts by priority */
#define APIC_ARBITRATION_PRIORITY   0x090   /* Arbitration Priority Register (APR) — read-only */
#define APIC_PROCESSOR_PRIORITY     0x0A0   /* Processor Priority Register (PPR) — read-only */
#define APIC_EOI                    0x0B0   /* End-Of-Interrupt — write 0 to signal interrupt completion */
#define APIC_SPURIOUS_VECTOR        0x0F0   /* Spurious Interrupt Vector Register (SVR) */
#define APIC_ERROR_STATUS           0x280   /* Error Status Register (ESR) */
#define APIC_LVT_CMCI               0x2F0   /* LVT: Corrected Machine Check Interrupt */
#define APIC_ICR_LOW                0x300   /* Interrupt Command Register (low 32 bits) */
#define APIC_ICR_HIGH               0x310   /* Interrupt Command Register (high 32 bits — destination field) */
#define APIC_LVT_TIMER              0x320   /* LVT: Timer */
#define APIC_LVT_THERMAL            0x330   /* LVT: Thermal Monitor */
#define APIC_LVT_PERF_MONITORING    0x340   /* LVT: Performance Monitoring */
#define APIC_LVT_INT0               0x350   /* LVT: External Pin 0 (LINT0) */
#define APIC_LVT_INT1               0x360   /* LVT: External Pin 1 (LINT1) */
#define APIC_LVT_INT2               0x370   /* LVT: External Pin 2 (LINT2) */
#define APIC_LVT_ERROR              0x380   /* LVT: Error */
#define APIC_TIMER_INIT_COUNT       0x390   /* Timer Initial Count register */
#define APIC_TIMER_CUR_COUNT        0x3A0   /* Timer Current Count register (counts down) */
#define APIC_TIMER_DIVIDE           0x3E0   /* Timer Divide Configuration register */

/*
 * ── I/O APIC Register Offsets ────────────────────────────────────────
 * The I/O APIC is an MMIO device mapped at 0xFEC00000 by default.
 * Accessed via IOREGSEL (offset 0x00) + IOWIN (offset 0x10) indirect protocol.
 */

#define IO_APIC_ID                  0x00    /* I/O APIC ID register */
#define IO_APIC_VERSION             0x01    /* Version register (bits 0-7 = version, 16-23 = max redir entry) */
#define IO_APIC_ARBITRATION        0x02    /* Arbitration ID register */
#define IO_APIC_REDIR_TABLE_START  0x10    /* Start of Redirection Table entries (each entry = 2 regs) */

/*
 * ── MSR and Memory Map Addresses ─────────────────────────────────────
 */

#define APIC_BASE_MSR               0x1B    /* MSR to read/write the local APIC base address */
#define APIC_BASE_DEFAULT           0xFEE00000UL  /* Default physical address of the local APIC */
#define IO_APIC_ADDRESS             0xFEC00000UL  /* Default physical address of the I/O APIC */

/*
 * ── Spurious Vector Register (SVR) Bits ──────────────────────────────
 */

#define APIC_SPURIOUS_ENABLE       0x100   /* Bit 8: enable the local APIC */
#define APIC_SPURIOUS_DEFAULT_VECTOR 0xFF  /* Default spurious interrupt vector */

/*
 * ── Timer Mode ───────────────────────────────────────────────────────
 */

#define APIC_TIMER_ONE_SHOT        0x00    /* Timer fires once, then stops */
#define APIC_TIMER_PERIODIC        0x01    /* Timer fires repeatedly at the initial count rate */
#define APIC_TIMER_TSC_DEADLINE    0x02    /* Timer uses the TSC deadline mechanism (x2APIC only) */

/*
 * ── Interrupt Command Register (ICR) Bits ────────────────────────────
 * Used to send IPIs (Inter-Processor Interrupts) between cores.
 */

#define APIC_ICR_DEST_SHIFT        18      /* Bits 18-19: destination shorthand */
#define APIC_ICR_LEVEL             0x4000  /* Bit 14: level (0 = de-assert, 1 = assert) */
#define APIC_ICR_ASSERT            0x2000  /* Bit 13: trigger mode (0 = edge, 1 = level) */
#define APIC_ICR_TRIG_MODE         0x4000  /* Bit 15: 0 = edge, 1 = level */
#define APIC_ICR_DEST_SELF          0x40000  /* Destination shorthand: self */
#define APIC_ICR_DEST_ALL           0x80000  /* Destination shorthand: all including self */
#define APIC_ICR_DEST_ALL_SELF      0xC0000  /* Destination shorthand: all excluding self */
#define APIC_ICR_INIT              0x00050000  /* INIT IPI (delivery mode 5) */
#define APIC_ICR_STARTUP           0x00060000  /* STARTUP IPI (delivery mode 6) */
#define APIC_ICR_DELIVERY_SHIFT    8       /* Bits 8-10: delivery mode */

/*
 * ── ICR Delivery Modes ───────────────────────────────────────────────
 */

#define APIC_DELIVERY_FIXED        0       /* Deliver to all processors listed in destination */
#define APIC_DELIVERY_LOWEST       1       /* Deliver to the processor with lowest priority */
#define APIC_DELIVERY_SMI          2       /* System Management Interrupt */
#define APIC_DELIVERY_NMI           4       /* Non-Maskable Interrupt */
#define APIC_DELIVERY_INIT         5       /* INIT IPI (reset sequence) */
#define APIC_DELIVERY_STARTUP      6       /* Startup IPI (bring up AP cores) */
#define APIC_DELIVERY_EXTINT       7       /* External interrupt (delivered through PIC) */

/*
 * ── APM / Power Management Ports ─────────────────────────────────────
 * Old-style APM (Advanced Power Management) via I/O ports.
 * Used for reboot/shutdown on some hardware.
 */

#define APM_CNT_PORT               0xB2    /* APM command port */
#define APM_DATA_PORT              0xB3    /* APM data port */

/*
 * APM commands
 */
#define APM_CNT_DISCONNECT         0x00    /* Disconnect from APM interface */
#define APM_CNT_GET_INFO          0x01    /* Get APM info */
#define APM_CNT_GET_PM_STATE      0x03    /* Get power management state */
#define APM_CNT_ENABLE             0x10    /* Enable APM */
#define APM_CNT_DISABLE           0x11    /* Disable APM */
#define APM_CNT_STANDBY           0x14    /* Enter standby mode */
#define APM_CNT_SUSPEND           0x15    /* Enter suspend mode */
#define APM_CNT_OFF               0x18    /* Turn system off */

/*
 * APM return states
 */
#define APM_STATE_ON               0x00    /* System is on */
#define APM_STATE_STANDBY          0x01    /* System is in standby */
#define APM_STATE_SUSPEND          0x02    /* System is in suspend */
#define APM_STATE_OFF              0x03    /* System is off */

/*
 * ── CMOS Shutdown Register ───────────────────────────────────────────
 * CMOS register 0x0F controls the shutdown action after reset.
 * Used to implement reboot via keyboard controller + CMOS.
 */

#define CMOS_REG_SHUTDOWN          0x0F    /* CMOS shutdown status byte */
#define CMOS_SHUTDOWN_JUMP        0x0A    /* Shutdown action: jump via INT 0x19 (reboot) */
#define CMOS_SHUTDOWN_NMI         0x08    /* Shutdown action: NMI + reboot */

/*
 * ── Function Prototypes ──────────────────────────────────────────────
 * These are implemented in apic.c.
 */

/* Local APIC */
bool apic_init(void);
bool apic_is_enabled(void);
bool apic_uses_ioapic(void);
void apic_enable(void);
void apic_disable(void);
uint32_t apic_get_id(void);
uint32_t apic_get_version(void);
void apic_send_eoi(void);
void apic_set_spurious_vector(uint8_t vector);
void apic_timer_init(uint32_t divisor, uint32_t count);
void apic_timer_stop(void);
uint32_t apic_read(uint32_t offset);
void apic_write(uint32_t offset, uint32_t value);

/* I/O APIC */
bool ioapic_init(void);
void ioapic_set_irq(uint32_t gsi, uint8_t vector, uint8_t polarity, bool level);
void ioapic_enable_irq(uint32_t gsi);
void ioapic_disable_irq(uint32_t gsi);
uint32_t ioapic_read(uint32_t offset);
void ioapic_write(uint32_t offset, uint32_t value);

/* Power management */
void system_reboot(void);
void system_shutdown(void);
void system_poweroff(void);

/* Scheduled shutdown */
bool get_shutdown_pending(void);
int get_shutdown_seconds(void);
bool is_shutdown_cancelled(void);
void cancel_shutdown(void);
void initiate_shutdown(int seconds);

#endif
