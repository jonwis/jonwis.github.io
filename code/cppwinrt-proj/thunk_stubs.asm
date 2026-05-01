; thunk_stubs.asm - Compact vtable thunk stubs for generic interface caching
;
; Each stub is 10 bytes: mov eax, <slot_index> + jmp common_thunk_dispatch
; The common dispatch function:
;   1. Saves the caller's register args (rdx, r8, r9)
;   2. Calls InterfaceThunk::resolve() (rcx = thunk 'this' already in place)
;   3. Loads the real vtable, indexes by eax (slot index)
;   4. Restores caller's args, sets rcx = real object, tail-jumps to real method
;
; InterfaceThunk layout (must match C++ struct):
;   [+0]  vtable pointer (void const* const*)
;   [+8]  default_abi (void*)
;   [+16] cache_slot (void**)
;   [+24] iid (GUID const*)
;
; Total binary size: 256 * 10 + ~100 = ~2660 bytes (vs ~20KB for template approach)

extern generic_mutating_resolve_thunk:proc

_TEXT segment align(16)

; ============================================================================
; Common dispatch - entered with eax = vtable slot index, rcx = InterfaceThunk*
; The caller's original args are still in rdx, r8, r9, and on the stack.
; ============================================================================
align 16
common_thunk_dispatch proc
    ; Save caller's register args - we need them after resolve()
    mov     [rsp+10h], rdx      ; shadow slot for rdx (caller allocated)
    mov     [rsp+18h], r8       ; shadow slot for r8
    mov     [rsp+20h], r9       ; shadow slot for r9
    push    rax                 ; save slot index
    sub     rsp, 20h            ; shadow space for resolve call

    ; rcx = InterfaceThunk* (already in place from the original call)
    call    generic_mutating_resolve_thunk  ; returns real interface ptr in rax

    add     rsp, 20h
    pop     r10                 ; r10 = slot index

    ; Load the real vtable and index to the target method
    mov     rcx, rax            ; rcx = real interface 'this'
    mov     r11, [rax]          ; r11 = real vtable base
    mov     r11, [r11 + r10*8]  ; r11 = real_vtable[slot_index]

    ; Restore caller's args from shadow space
    mov     rdx, [rsp+10h]
    mov     r8,  [rsp+18h]
    mov     r9,  [rsp+20h]

    ; Tail-jump to the real method. The caller's stack frame (including
    ; any args beyond r9) is untouched, so the real method sees exactly
    ; the same stack layout it expects.
    jmp     r11
common_thunk_dispatch endp

; ============================================================================
; Macro to emit a single thunk stub
; ============================================================================
thunk_stub macro idx
    align 2
    generic_mutating_thunk_stub_&idx& proc
        mov     eax, idx
        jmp     common_thunk_dispatch
    generic_mutating_thunk_stub_&idx& endp
endm

; ============================================================================
; Emit 256 thunk stubs (slots 0-255)
; ============================================================================
counter = 0
rept 256
    thunk_stub %counter
    counter = counter + 1
endm

_TEXT ends

; ============================================================================
; Read-only data: the vtable array of 256 stub pointers, exported as
; generic_mutating_thunk_vtable for C++ to reference.
; ============================================================================

vtable_entry macro idx
    dq generic_mutating_thunk_stub_&idx&
endm

_DATA segment

    public generic_mutating_thunk_vtable
    generic_mutating_thunk_vtable label qword

    counter2 = 0
    rept 256
        vtable_entry %counter2
        counter2 = counter2 + 1
    endm

_DATA ends
end
