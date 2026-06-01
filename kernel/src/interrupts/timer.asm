[BITS 64]

global timer_pit_config   ; Declare the PIT configuration function as global

section .text

; ----------------------------
; PIT Configuration
; ----------------------------
; This function configures the PIT (Programmable Interval Timer)
; to generate regular IRQs (Interrupt Requests).

timer_pit_config:
    ; Send control byte to port 0x43: Mode 3 (Rate Generator), 16-bit
    mov al, 0x36          ; Mode 3: Rate Generator
    out 0x43, al          ; Send control byte to port 0x43

    ; Set counter value (configured to trigger the timer every 1 ms)
    mov ax, 0x9C          ; Counter low byte (LSB)
    out 0x40, al          ; Send low byte to port 0x40
    mov al, 0x2F          ; Counter high byte (MSB)
    out 0x40, al          ; Send high byte to port 0x40

    ret                    ; Return from the function
