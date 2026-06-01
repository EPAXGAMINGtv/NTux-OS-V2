#include <interrupt/pic.h>
#include <arch/x86_64/io.h>


void pic_send_command(uint8_t cmd,uint16_t port){
    outb(port,cmd);
}

void pic_send_data(uint8_t data,uint16_t port){
    outb(port,data);
}

void pic_init(void) {
    /* Route interrupts to the PIC (IMCR) when present. */
    outb(0x22, 0x70);
    outb(0x23, 0x00);

    pic_send_command(0x11, MASTER_PIC_CMD);  
    pic_send_command(0x11, SLAVE_PIC_CMD);   
    pic_send_data(0x20, MASTER_PIC_DATA);  
    pic_send_data(0x28, SLAVE_PIC_DATA);   
    pic_send_data(0x04, MASTER_PIC_DATA);  
    pic_send_data(0x02, SLAVE_PIC_DATA);   
    pic_send_data(0x01, MASTER_PIC_DATA);  
    pic_send_data(0x01, SLAVE_PIC_DATA);    
    pic_send_data(0xFF, MASTER_PIC_DATA);
    pic_send_data(0xFF, SLAVE_PIC_DATA); 
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(SLAVE_PIC_CMD, 0x20);
    }
    outb(MASTER_PIC_CMD, 0x20);
}

void pic_set_mask(uint8_t irq_line) {
    uint16_t port;
    uint8_t value;

    if (irq_line < 8) {
        port = MASTER_PIC_DATA;
    } else {
        port = SLAVE_PIC_DATA;
        irq_line = (uint8_t)(irq_line - 8);
    }

    value = inb(port) | (uint8_t)(1u << irq_line);
    outb(port, value);
}

void pic_clear_mask(uint8_t irq_line) {
    uint16_t port;
    uint8_t value;

    if (irq_line < 8) {
        port = MASTER_PIC_DATA;
    } else {
        port = SLAVE_PIC_DATA;
        irq_line = (uint8_t)(irq_line - 8);
    }

    value = inb(port) & (uint8_t)~(1u << irq_line);
    outb(port, value);
}
