#ifndef APIC_H
#define APIC_H

#include <stdint.h>
#include <stdbool.h>


#define APIC_ID                     0x020  
#define APIC_VERSION                0x030  
#define APIC_TASK_PRIORITY          0x080  
#define APIC_ARBITRATION_PRIORITY   0x090  
#define APIC_PROCESSOR_PRIORITY     0x0A0  
#define APIC_EOI                    0x0B0  
#define APIC_SPURIOUS_VECTOR        0x0F0  
#define APIC_ERROR_STATUS           0x280  
#define APIC_LVT_CMCI               0x2F0  
#define APIC_ICR_LOW                0x300  
#define APIC_ICR_HIGH               0x310  
#define APIC_LVT_TIMER              0x320  
#define APIC_LVT_THERMAL            0x330  
#define APIC_LVT_PERF_MONITORING    0x340  
#define APIC_LVT_INT0               0x350  
#define APIC_LVT_INT1               0x360  
#define APIC_LVT_INT2               0x370  
#define APIC_LVT_ERROR              0x380  
#define APIC_TIMER_INIT_COUNT       0x390  
#define APIC_TIMER_CUR_COUNT        0x3A0  
#define APIC_TIMER_DIVIDE           0x3E0  


#define IO_APIC_ID                  0x00   
#define IO_APIC_VERSION             0x01   
#define IO_APIC_ARBITRATION        0x02   
#define IO_APIC_REDIR_TABLE_START  0x10   


#define APIC_BASE_MSR               0x1B   
#define APIC_BASE_DEFAULT           0xFEE00000UL  
#define IO_APIC_ADDRESS             0xFEC00000UL  


#define APIC_SPURIOUS_ENABLE       0x100  
#define APIC_SPURIOUS_DEFAULT_VECTOR 0xFF


#define APIC_TIMER_ONE_SHOT        0x00   
#define APIC_TIMER_PERIODIC        0x01   
#define APIC_TIMER_TSC_DEADLINE    0x02   


#define APIC_ICR_DEST_SHIFT        18
#define APIC_ICR_LEVEL             0x4000  
#define APIC_ICR_ASSERT            0x2000  
#define APIC_ICR_TRIG_MODE         0x4000  
#define APIC_ICR_DEST_SELF          0x40000  
#define APIC_ICR_DEST_ALL           0x80000  
#define APIC_ICR_DEST_ALL_SELF      0xC0000  
#define APIC_ICR_INIT              0x00050000  
#define APIC_ICR_STARTUP           0x00060000  
#define APIC_ICR_DELIVERY_SHIFT    8


#define APIC_DELIVERY_FIXED        0
#define APIC_DELIVERY_LOWEST       1
#define APIC_DELIVERY_SMI          2
#define APIC_DELIVERY_NMI           4
#define APIC_DELIVERY_INIT         5
#define APIC_DELIVERY_STARTUP      6
#define APIC_DELIVERY_EXTINT       7




#define APM_CNT_PORT               0xB2    
#define APM_DATA_PORT              0xB3    


#define APM_CNT_DISCONNECT         0x00
#define APM_CNT_GET_INFO          0x01
#define APM_CNT_GET_PM_STATE      0x03
#define APM_CNT_ENABLE             0x10
#define APM_CNT_DISABLE           0x11
#define APM_CNT_STANDBY           0x14
#define APM_CNT_SUSPEND           0x15
#define APM_CNT_OFF               0x18


#define APM_STATE_ON               0x00
#define APM_STATE_STANDBY          0x01
#define APM_STATE_SUSPEND          0x02
#define APM_STATE_OFF              0x03


#define CMOS_REG_SHUTDOWN          0x0F    
#define CMOS_SHUTDOWN_JUMP        0x0A    
#define CMOS_SHUTDOWN_NMI         0x08    




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


bool ioapic_init(void);
void ioapic_set_irq(uint32_t gsi, uint8_t vector, uint8_t polarity, bool level);
void ioapic_enable_irq(uint32_t gsi);
void ioapic_disable_irq(uint32_t gsi);
uint32_t ioapic_read(uint32_t offset);
void ioapic_write(uint32_t offset, uint32_t value);



void system_reboot(void);
void system_shutdown(void);
void system_poweroff(void);


bool get_shutdown_pending(void);
int get_shutdown_seconds(void);
bool is_shutdown_cancelled(void);
void cancel_shutdown(void);
void initiate_shutdown(int seconds);


#endif 
