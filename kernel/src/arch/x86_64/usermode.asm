global enter_user
enter_user:
    
    mov ax, 0x23          
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    
    
    
    
    
    
    
    push 0x23             
    push rdi              
    push qword 0x202      
    push 0x1B             
    push rsi              
    
    
    iretq                 
