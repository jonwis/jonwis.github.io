; thunk_stubs_arm64.asm - ARM64 thunk stubs for generic interface caching
;
; ARM64 calling convention: x0-x7 are integer args, x0 = 'this'.
; Each stub is 8 bytes (2 instructions): compute slot index from PC-relative
; offset to stub base, then branch to common dispatch.
;
; The common dispatch saves x1-x7 and lr, calls resolve(x0), then
; restores args and tail-branches to the real vtable slot.

    IMPORT winrt_fast_resolve_thunk

    AREA |.text|, CODE, READONLY, ALIGN=4

; ============================================================================
; Common dispatch - entered with w10 = vtable slot index, x0 = InterfaceThunk*
; x1-x7 = caller's original args, lr = caller's return address
; ============================================================================
    ALIGN 16
common_thunk_dispatch PROC
    stp     x29, x30, [sp, #-80]!
    mov     x29, sp
    stp     x1, x2, [sp, #16]
    stp     x3, x4, [sp, #32]
    stp     x5, x6, [sp, #48]
    stp     x7, x10, [sp, #64]

    ; x0 = InterfaceThunk*
    bl      winrt_fast_resolve_thunk

    ldp     x7, x10, [sp, #64]
    ldp     x5, x6, [sp, #48]
    ldp     x3, x4, [sp, #32]
    ldp     x1, x2, [sp, #16]
    ldp     x29, x30, [sp], #80

    ; x0 = real 'this' for the target method
    ldr     x9, [x0]
    ldr     x9, [x9, x10, lsl #3]
    br      x9
    ENDP

; ============================================================================
; Each stub computes its slot index from (PC - stub_base) / 8.
; All stubs are 8 bytes (2 x 4-byte instructions), tightly packed.
; ============================================================================
    ALIGN 8
    EXPORT winrt::fast_thunk_stub_base
winrt::fast_thunk_stub_base

    ; Macro emits one stub: adr x10 to self, sub from base, lsr by 3, branch
    ; Actually each stub needs to be exactly 8 bytes = 2 instructions.
    ; Use: adr x10, . ; then compute index at dispatch.
    ; Simpler: just use literal pool. Each stub = ldr w10, [pc, #4]; b dispatch; DCD index
    ; That's 12 bytes per stub though.
    ;
    ; Best approach for 8 bytes: movz w10, #imm16 ; b common_thunk_dispatch
    ; We just need to emit the right immediate. Use DCI to encode the instructions directly.

    MACRO
    ThunkStubDCD $idx
    EXPORT winrt::fast_thunk_stub_$idx
winrt::fast_thunk_stub_$idx
    DCD CurEnc
    b       common_thunk_dispatch
    MEND

    GBLA StubCtr
    GBLA CurEnc
StubCtr SETA 0
    WHILE StubCtr < 256
CurEnc SETA 0x5280000A :OR: (StubCtr:SHL:5)
    ThunkStubDCD $StubCtr
StubCtr SETA StubCtr + 1
    WEND

; ============================================================================
; Vtable array of 256 stub pointers (8 bytes each)
; ============================================================================
    AREA |.data|, DATA, READWRITE, ALIGN=3

    EXPORT winrt::fast_thunk_vtable
winrt::fast_thunk_vtable

    MACRO
    VtableEntry $idx
    DCQ winrt::fast_thunk_stub_$idx
    MEND

    GBLA VtblCtr
VtblCtr SETA 0
    WHILE VtblCtr < 256
    VtableEntry $VtblCtr
VtblCtr SETA VtblCtr + 1
    WEND

    END
