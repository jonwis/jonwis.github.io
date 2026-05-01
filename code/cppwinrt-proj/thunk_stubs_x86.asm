; thunk_stubs_x86.asm - x86 (32-bit) thunk stubs for generic interface caching
;
; x86 COM uses __stdcall: args on stack, callee cleans. Our thunks don't need
; to know the arg count because we tail-jump (jmp, not call) to the real method,
; which performs the stdcall cleanup itself.
;
; Each stub is 7 bytes: mov eax, <slot_index> + jmp common_thunk_dispatch
; The common dispatch replaces 'this' on the caller's stack with the real
; interface pointer and tail-jumps to the real vtable slot.
;
; Stack on entry to stub (from COM caller):
;   [esp]   = return address
;   [esp+4] = this (InterfaceThunk*)
;   [esp+8] = arg1
;   [esp+C] = arg2 ...

.686
.model flat, c

extern generic_mutating_resolve_thunk:proc

.code

; ============================================================================
; Common dispatch - entered with eax = vtable slot index
; ============================================================================
align 16
common_thunk_dispatch proc
    ; Stack: [esp]=ret_addr [esp+4]=this [esp+8]=arg1 ...

    push    eax                     ; save slot index
    ; Stack: [esp]=slot [esp+4]=ret_addr [esp+8]=this [esp+0C]=arg1 ...

    push    dword ptr [esp+8]       ; push 'this' (InterfaceThunk*) as arg for resolve
    ; Stack: [esp]=this [esp+4]=slot [esp+8]=ret_addr [esp+0C]=this [esp+10]=arg1 ...

    call    generic_mutating_resolve_thunk  ; cdecl, returns real ptr in eax
    add     esp, 4                  ; clean cdecl arg
    ; Stack: [esp]=slot [esp+4]=ret_addr [esp+8]=this [esp+0C]=arg1 ...

    pop     ecx                     ; ecx = slot index
    ; Stack: [esp]=ret_addr [esp+4]=this [esp+8]=arg1 ...

    mov     [esp+4], eax            ; replace 'this' with real interface ptr
    mov     edx, [eax]              ; edx = real vtable base
    jmp     dword ptr [edx + ecx*4] ; tail-jump to real method (4-byte vtable slots on x86)
common_thunk_dispatch endp

; ============================================================================
; Thunk stub macro - each stub is: mov eax, <N>; jmp common_thunk_dispatch
; ============================================================================
thunk_stub macro idx
    align 2
    generic_mutating_thunk_stub_&idx& proc
        mov     eax, idx
        jmp     common_thunk_dispatch
    generic_mutating_thunk_stub_&idx& endp
endm

; ============================================================================
; Emit 256 thunk stubs
; ============================================================================
counter = 0
rept 256
    thunk_stub %counter
    counter = counter + 1
endm

; ============================================================================
; Vtable array of 256 stub pointers (4 bytes each on x86)
; ============================================================================
.data

vtable_entry macro idx
    dd generic_mutating_thunk_stub_&idx&
endm

public generic_mutating_thunk_vtable
generic_mutating_thunk_vtable label dword

counter2 = 0
rept 256
    vtable_entry %counter2
    counter2 = counter2 + 1
endm

end
