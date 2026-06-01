#include <drivers/audio/audio.h>
#include <arch/x86_64/io.h>
#include <drivers/framebuffer/kprint.h>

static uint8_t speaker_was_on = 0;

void beep(uint32_t freq_hz, uint32_t duration_ms)
{
    if (freq_hz == 0) {
        
        outb(0x61, inb(0x61) & 0xFC);
        speaker_was_on = 0;
        return;
    }

    
    uint8_t tmp = inb(0x61);
    if (!(tmp & 0x03)) {
        outb(0x61, tmp | 0x03);   
        speaker_was_on = 1;
    }

    
    uint32_t divisor = 1193180 / freq_hz;
    outb(0x43, 0xB6);               
    outb(0x42, divisor & 0xFF);
    outb(0x42, divisor >> 8);

    
    
    for (volatile uint64_t i = 0; i < duration_ms * 1000000ULL; i++);

    
    if (speaker_was_on) {
        outb(0x61, inb(0x61) & 0xFC);
        speaker_was_on = 0;
    }
}

void play_startup_sound(void)
{
    beep(523, 180);   
    beep(659, 180);   
    beep(784, 220);   

    beep(1047, 18);  
    beep(988, 100);   
    beep(0, 100);
}

void play_shutdown_sound(void)
{
    beep(1318, 200);  
    beep(1175, 220);  
    beep(1047, 260);  
    beep( 880, 320);  
    beep( 698, 420);  


    beep(0, 200);
    kprint("Goodbye...\n");
}
void play_slainewin_easteregg(void)
{
    for (uint64_t t = 0; t < 8000 * 60; t++) {  
        uint32_t v = (t * ((t / 2 >> 10 | t % 16 * t >> 8) & 8 * t >> 12 & 18)) | (-(t / 16) + 64);
        uint32_t freq = (v & 0xFF) + 150;  
        if (freq > 150) {
            uint32_t div = 1193180 / freq;  
            outb(0x43, 0xB6); 
            outb(0x42, (uint8_t)div);  
            outb(0x42, div >> 8);  
            outb(0x61, inb(0x61) | 3); 
        }

        for (volatile int d = 0; d < 125; d++) asm volatile ("nop");  
        if ((t % 8000) == 7999) {
            outb(0x61, inb(0x61) & 0xFC);  
            for (volatile int d = 0; d < 3000; d++) asm("nop");  
        }
    }
    outb(0x61, inb(0x61) & 0xFC);
    kprint("SlaineWin Bytebeat sound completed\n");
}