[BITS 64]






global inb         
global outb        
global inw         
global outw        
global inl         
global outl        
global ind
global outd

section .text



inb:
    and rdi, 0xFFFF       
    mov dx, di            
    in  al, dx            
    ret                   



outb:
    and rdi, 0xFFFF       
    mov dx, di            
    mov al, sil           
    out dx, al            
    ret                   



inw:
    and rdi, 0xFFFF       
    mov dx, di            
    in  ax, dx            
    ret                   



outw:
    and rdi, 0xFFFF       
    mov dx, di            
    mov ax, si            
    out dx, ax            
    ret                   

inl:
    mov dx, di          
    in  eax, dx         
    ret


outl:
    mov dx, di          
    mov eax, esi       
    out dx, eax         
    ret


ind:
    mov dx, di          
    in  eax, dx         
    ret                 



outd:
    mov dx, di          
    mov eax, esi        
    out dx, eax         
    ret                 