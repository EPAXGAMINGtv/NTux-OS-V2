#include <arch/x86_64/cpu.h>
#include <drivers/framebuffer/kprint.h>

void core_init(void) {
    kprint_ok("SMP disabled - single core mode");
}
