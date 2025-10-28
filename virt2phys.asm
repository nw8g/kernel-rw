section .text
global AsmCachedVirtToPhys
global AsmCacheIndex

extern MmCopyMemory

%define CACHE_SIZE 4096
%define PAGE_MASK 0xFFFFFFFFF000

%define PTE_CACHE_ENTRY.virtPage 0
%define PTE_CACHE_ENTRY.physPage 8
%define PTE_CACHE_ENTRY_SIZE 16

; ============================================================================
; int AsmCacheIndex(UINT64 virtPage)
; RCX = virtPage
; Returns: cache index in RAX
; ============================================================================
AsmCacheIndex:
    mov rax, 0xcbf29ce484222325    ; FNV offset basis
    xor rax, rcx                    ; hash ^= virtPage
    mov rdx, 0x100000001b3          ; FNV prime
    imul rax, rdx                   ; hash *= prime
    and rax, CACHE_SIZE - 1         ; modulo by cache size (power of 2)
    ret

; ============================================================================
; UINT64 AsmCachedVirtToPhys(UINT64 cr3, UINT64 va, PTE_CACHE_ENTRY* cache)
; RCX = cr3
; RDX = va
; R8  = cache pointer
; Returns: physical address in RAX (0 if failed)
; ============================================================================
AsmCachedVirtToPhys:
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 48                     ; Shadow space + locals
    
    ; Save parameters
    mov r12, rcx                    ; r12 = cr3
    mov r13, rdx                    ; r13 = va
    mov r14, r8                     ; r14 = cache
    
    ; Calculate virtPage = va >> 12
    mov rax, r13
    shr rax, 12
    mov r15, rax                    ; r15 = virtPage
    
    ; Get cache index
    mov rcx, r15
    call AsmCacheIndex
    mov rbx, rax                    ; rbx = cache index
    
    ; Calculate cache entry address: cache + (index * 16)
    shl rbx, 4                      ; rbx *= sizeof(PTE_CACHE_ENTRY)
    add rbx, r14                    ; rbx = &cache[index]
    
    ; Check if cache hit: entry.virtPage == virtPage
    mov rax, qword [rbx + PTE_CACHE_ENTRY.virtPage]
    cmp rax, r15
    jne .cache_miss
    
    ; Cache HIT - return physPage + offset
    mov rax, qword [rbx + PTE_CACHE_ENTRY.physPage]
    mov rcx, r13
    and rcx, 0xFFF                  ; offset = va & 0xFFF
    add rax, rcx
    jmp .done

.cache_miss:
    ; Clean CR3
    and r12, 0xFFFFFFFFFFFFFFF0     ; cr3 &= ~0xF
    
    ; Extract page table indices
    mov rsi, r13
    and rsi, 0xFFF                  ; rsi = offset = va & 0xFFF
    
    mov rax, r13
    shr rax, 12
    and rax, 0x1FF                  ; pte index
    mov qword [rsp + 32], rax
    
    mov rax, r13
    shr rax, 21
    and rax, 0x1FF                  ; pt index
    mov qword [rsp + 24], rax
    
    mov rax, r13
    shr rax, 30
    and rax, 0x1FF                  ; pd index
    mov qword [rsp + 16], rax
    
    mov rax, r13
    shr rax, 39
    and rax, 0x1FF                  ; pdp index
    mov rdi, rax
    
    ; Walk page tables
    ; Level 4: PML4
    mov rax, rdi
    shl rax, 3                      ; * 8
    add rax, r12                    ; cr3 + pdp*8
    mov rcx, rax
    call ReadPhysQword
    test rax, rax
    jz .fail
    test rax, 1                     ; Check present bit
    jz .fail
    and rax, PAGE_MASK
    mov r12, rax                    ; r12 = PDP base
    
    ; Level 3: PDP
    mov rax, qword [rsp + 16]       ; pd index
    shl rax, 3
    add rax, r12
    mov rcx, rax
    call ReadPhysQword
    test rax, rax
    jz .fail
    test rax, 1
    jz .fail
    
    ; Check 1GB page
    test rax, 0x80                  ; PS bit
    jz .not_1gb
    
    ; 1GB page - extract address and update cache
    mov rcx, rax
    mov rax, 0xFFFFFFFFC0000000     ; Mask for 1GB page
    and rax, rcx
    mov rdx, r13
    and rdx, 0x3FFFFFFF             ; Offset within 1GB
    add rax, rdx
    
    ; Update cache
    mov qword [rbx + PTE_CACHE_ENTRY.virtPage], r15
    mov qword [rbx + PTE_CACHE_ENTRY.physPage], rax
    jmp .done
    
.not_1gb:
    and rax, PAGE_MASK
    mov r12, rax                    ; r12 = PD base
    
    ; Level 2: PD
    mov rax, qword [rsp + 24]       ; pt index
    shl rax, 3
    add rax, r12
    mov rcx, rax
    call ReadPhysQword
    test rax, rax
    jz .fail
    test rax, 1
    jz .fail
    
    ; Check 2MB page
    test rax, 0x80
    jz .not_2mb
    
    ; 2MB page
    and rax, PAGE_MASK
    mov rdx, r13
    and rdx, 0x1FFFFF               ; Offset within 2MB
    add rax, rdx
    
    ; Update cache
    mov qword [rbx + PTE_CACHE_ENTRY.virtPage], r15
    mov qword [rbx + PTE_CACHE_ENTRY.physPage], rax
    jmp .done
    
.not_2mb:
    and rax, PAGE_MASK
    mov r12, rax                    ; r12 = PT base
    
    ; Level 1: PT
    mov rax, qword [rsp + 32]       ; pte index
    shl rax, 3
    add rax, r12
    mov rcx, rax
    call ReadPhysQword
    test rax, rax
    jz .fail
    test rax, 1
    jz .fail
    
    ; 4KB page
    and rax, PAGE_MASK
    add rax, rsi                    ; + offset
    
    ; Update cache
    mov qword [rbx + PTE_CACHE_ENTRY.virtPage], r15
    mov qword [rbx + PTE_CACHE_ENTRY.physPage], rax
    jmp .done

.fail:
    xor rax, rax

.done:
    add rsp, 48
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret

; ============================================================================
; Helper: Read QWORD from physical address
; RCX = physical address
; Returns: QWORD in RAX (0 on failure)
; ============================================================================
ReadPhysQword:
    push rbx
    push rsi
    sub rsp, 56                     ; Shadow + MM_COPY_ADDRESS + output
    
    ; Setup MM_COPY_ADDRESS on stack
    mov qword [rsp + 32], rcx       ; PhysicalAddress.QuadPart
    
    ; out buffer
    lea rax, [rsp + 48]
    
    ; MmCopyMemory(out, mm, 8, MM_COPY_MEMORY_PHYSICAL, &bytesRead)
    lea r9, [rsp + 40]              ; &bytesRead
    mov qword [rsp + 32], 2         ; MM_COPY_MEMORY_PHYSICAL = 2
    mov r8, 8                       ; length
    lea rdx, [rsp + 32]             ; &MM_COPY_ADDRESS
    mov rcx, rax                    ; out
    call MmCopyMemory
    
    test eax, eax
    js .read_fail
    
    ; Check if we read 8 bytes
    cmp qword [rsp + 40], 8
    jne .read_fail
    
    mov rax, qword [rsp + 48]       ; Return the value
    jmp .read_done
    
.read_fail:
    xor rax, rax
    
.read_done:
    add rsp, 56
    pop rsi
    pop rbx
    ret