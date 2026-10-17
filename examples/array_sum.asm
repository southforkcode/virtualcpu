; Working ISA, origin $0100; CS=DS=0, valid empty stack, installed RAM.
; Sum five words at DS:$2000 to DS:$2100 modulo 65536; accepts a zero count.
; r4 is a standalone scratch pointer and finishes at $2000+2*count.
; The fixed input is ordinary RAM and the advanced pointer is representable.
        MOV.W #$2000, r4
        XOR   r0, r0
        MOV.B #5, r1
        TST.W r1
        BEQ   done
loop:
        MOV.W [r4+], r2
        ADD   r2, r0
        DEC.W r1
        BNE   loop
done:
        MOV.W r0, [$2100]
        HALT
