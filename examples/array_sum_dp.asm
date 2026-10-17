; Revision 0.4 standalone array sum, assembled at CS:$0100.
; CS=DS=0, I=0; code, data, and result storage are installed RAM.
; Input: five arbitrary words at DS:$2000 (e.g. 1,2,3,4,5).
; Change #5 to a count from 0 through 128 for this memory layout.
; Output: sum modulo 65536 at DS:$2100 and r0.
; Final DP=$2000+2*count, r1=0; other working registers may change.
;
; 37 bytes. The empty-array check is retained deliberately.
; XOR's N/Z updates are replaced by OR before the first flag-dependent branch.

        MOV.W #$2000, r2
        MOV   r2, dp
        XOR   r0, r0
        MOV.B #5, r1
        MOV.B #1, r3
        OR    r1, r1
        BEQ   done
loop:
        MOV.W [dp+0], r2
        ADD   r2, r0
        ADDP  #2, dp
        SUB   r3, r1
        BNE   loop
done:
        MOV.W r0, [$2100]
        HALT
