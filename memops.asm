section .text
global AsmFastMemCopy

; ============================================================================
; void AsmFastMemCopy(void* dst, const void* src, SIZE_T len)
; RCX = dst
; RDX = src
; R8  = len
; Optimized for small copies (<=64 bytes) using SSE/AVX
; ============================================================================
AsmFastMemCopy:
    test r8, r8
    jz .done
    
    cmp r8, 8
    jbe .copy_small
    
    cmp r8, 16
    jbe .copy_16
    
    cmp r8, 32
    jbe .copy_32
    
    cmp r8, 64
    jbe .copy_64
    
    ; Fallback to rep movsb for >64 bytes (shouldn't happen, maybe?)
    push rdi
    push rsi
    mov rdi, rcx
    mov rsi, rdx
    mov rcx, r8
    rep movsb
    pop rsi
    pop rdi
    jmp .done

.copy_small:
    ; 1-8 bytes: use overlapping QWORD reads
    mov rax, qword [rdx]
    mov qword [rcx], rax
    
    ; if len < 8
    cmp r8, 8
    je .done
    
    neg r8
    add r8, 8                       ; r8 = 8 - len
    mov rax, qword [rdx + r8]
    mov qword [rcx + r8], rax
    jmp .done

.copy_16:
    ; 9-16 bytes: two overlapping QWORDs
    mov rax, qword [rdx]
    mov r9, qword [rdx + 8]
    mov qword [rcx], rax
    mov qword [rcx + 8], r9
    
    sub r8, 16
    jz .done
    add r8, 16
    sub r8, 8
    mov rax, qword [rdx + r8]
    mov qword [rcx + r8], rax
    jmp .done

.copy_32:
    ; 17-32 bytes: use SSE (16-byte aligned moves)
    movdqu xmm0, [rdx]
    movdqu xmm1, [rdx + 16]
    movdqu [rcx], xmm0
    movdqu [rcx + 16], xmm1
    
    cmp r8, 32
    je .done
    sub r8, 32
    add r8, 16
    movdqu xmm0, [rdx + r8]
    movdqu [rcx + r8], xmm0
    jmp .done

.copy_64:
    ; 33-64 bytes: use SSE/AVX
    ; check if AVX is available (we'll assume SSE2 minimum)
    movdqu xmm0, [rdx]
    movdqu xmm1, [rdx + 16]
    movdqu xmm2, [rdx + 32]
    movdqu xmm3, [rdx + 48]
    
    movdqu [rcx], xmm0
    movdqu [rcx + 16], xmm1
    movdqu [rcx + 32], xmm2
    movdqu [rcx + 48], xmm3
    
    cmp r8, 64
    je .done
    sub r8, 64
    add r8, 48
    movdqu xmm0, [rdx + r8]
    movdqu [rcx + r8], xmm0

.done:
    ret