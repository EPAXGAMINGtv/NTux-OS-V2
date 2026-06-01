[BITS 64]

global timer_pit_config
extern timer_pit_config_c

section .text

timer_pit_config:
    call timer_pit_config_c
    ret
