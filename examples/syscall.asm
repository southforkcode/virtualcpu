; ISA revision 0.5 software trap example; not valid revision 0.4 assembly.
; Assemble/install caller at CS=0:$0100 and handler at CS=1:$0200 separately.
; Vector 6, physical $000018: 00 02 01 00 (handler 1:$0200).
; Valid saved-restorable stack limits required; no stack RAM is accessed.
; See doc/ISA.md for the ABI and doc/ISA_EXAMPLES.md for exact byte listings.

; Caller (9 bytes), segment 0, origin $0100:
        MOV.B #7, r0
        MOV.B #5, r1
        SWI   #1
        HALT

; Handler (42 bytes), segment 1, origin $0200:
; Example service 1: add two words; all other services return status 1.
        GETX  xtrap, r2
        MOV.B #1, r3
        CMP   r3, r2
        BNE   unsupported
        GETX  xr0, r0
        GETX  xr1, r1
        ADD   r1, r0
        SETX  r0, xr0
        XOR   r1, r1
        SETX  r1, xr1
        IRET
unsupported:
        XOR   r0, r0
        MOV.B #1, r1
        SETX  r0, xr0
        SETX  r1, xr1
        IRET
