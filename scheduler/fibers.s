bits 64
default rel

section .text

global internal_fiber_resume
global internal_fiber_suspend
global internal_fiber_exit
global internal_fiber_swap
global internal_fiber_start
global internal_fiber_on_return_jump

extern internal_fiber_on_return_c


%ifidn __OUTPUT_FORMAT__, win64
; win x64 ABI

internal_fiber_on_return_jump:
    mov rcx, rax
    call internal_fiber_on_return_c
    ud2

internal_fiber_resume:
    mov rsp, rcx

    movups xmm6,  [rsp + 0]
    movups xmm7,  [rsp + 16]
    movups xmm8,  [rsp + 32]
    movups xmm9,  [rsp + 48]
    movups xmm10, [rsp + 64]
    movups xmm11, [rsp + 80]
    movups xmm12, [rsp + 96]
    movups xmm13, [rsp + 112]
    movups xmm14, [rsp + 128]
    movups xmm15, [rsp + 144]
    add rsp, 160

    pop rax
    mov [gs:0x10], rax
    pop rax
    mov [gs:0x08], rax

    pop r15
    pop r14
    pop r13
    pop r12
    pop rsi
    pop rdi
    pop rbx
    pop rbp
    ret

internal_fiber_suspend:
    push rbp
    push rbx
    push rdi
    push rsi
    push r12
    push r13
    push r14
    push r15

    mov rax, [gs:0x08]
    push rax
    mov rax, [gs:0x10]
    push rax

    sub rsp, 160
    movups [rsp + 0],   xmm6
    movups [rsp + 16],  xmm7
    movups [rsp + 32],  xmm8
    movups [rsp + 48],  xmm9
    movups [rsp + 64],  xmm10
    movups [rsp + 80],  xmm11
    movups [rsp + 96],  xmm12
    movups [rsp + 112], xmm13
    movups [rsp + 128], xmm14
    movups [rsp + 144], xmm15

    mov [rcx], rsp

    mov rax, [rdx + 16]
    mov [gs:0x08], rax
    mov rax, [rdx + 24]
    mov [gs:0x10], rax

    mov rsp, [rdx + 0]
    jmp [rdx + 8]

internal_fiber_exit:
    mov rax, [rcx + 16]
    mov [gs:0x08], rax
    mov rax, [rcx + 24]
    mov [gs:0x10], rax

    mov rsp, [rcx + 0]
    jmp [rcx + 8]

internal_fiber_swap:
    push rbp
    push rbx
    push rdi
    push rsi
    push r12
    push r13
    push r14
    push r15

    mov rax, [gs:0x08]
    push rax
    mov rax, [gs:0x10]
    push rax

    sub rsp, 160
    movups [rsp + 0],   xmm6
    movups [rsp + 16],  xmm7
    movups [rsp + 32],  xmm8
    movups [rsp + 48],  xmm9
    movups [rsp + 64],  xmm10
    movups [rsp + 80],  xmm11
    movups [rsp + 96],  xmm12
    movups [rsp + 112], xmm13
    movups [rsp + 128], xmm14
    movups [rsp + 144], xmm15

    mov [rcx], rsp
    mov rsp, rdx

    movups xmm6,  [rsp + 0]
    movups xmm7,  [rsp + 16]
    movups xmm8,  [rsp + 32]
    movups xmm9,  [rsp + 48]
    movups xmm10, [rsp + 64]
    movups xmm11, [rsp + 80]
    movups xmm12, [rsp + 96]
    movups xmm13, [rsp + 112]
    movups xmm14, [rsp + 128]
    movups xmm15, [rsp + 144]
    add rsp, 160

    pop rax
    mov [gs:0x10], rax
    pop rax
    mov [gs:0x08], rax

    pop r15
    pop r14
    pop r13
    pop r12
    pop rsi
    pop rdi
    pop rbx
    pop rbp
    ret

; bootstrapping r12 = fn, r13 = arg; [rsp] = internal_fiber_on_return_jump
internal_fiber_start:
    mov rcx, r13
    jmp r12

%else
; linups v abi

internal_fiber_on_return_jump:
    mov rdi, rax
    call internal_fiber_on_return_c
    ud2

internal_fiber_resume:
    mov rsp, rdi
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

internal_fiber_suspend:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp
    mov rsp, [rsi + 0]
    jmp [rsi + 8]

internal_fiber_exit:
    mov rsp, [rdi + 0]
    jmp [rdi + 8]

internal_fiber_swap:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp
    mov rsp, rsi

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

; bootstrapping r12 = fn, r13 = arg; [rsp] = internal_fiber_on_return_jump
internal_fiber_start:
    mov rdi, r13
    jmp r12

%endif

