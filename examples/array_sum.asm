; Revision 0.4 standalone array sum, assembled at CS:$0100.
; CS=DS=0, I=0; code, data, and result storage are installed RAM.
; Input: five arbitrary words at DS:$2000 (e.g. 1,2,3,4,5).
; Change #5 to a count from 0 through 128 for this memory layout.
; Output: sum modulo 65536 at DS:$2100 and r0.
; Final r4=$2000+2*count, r1=0; DP is unchanged. Other working registers may change.
; Standalone program: r4 is not preserved as a callable function would require.
;
; 34 bytes. The empty-array check is retained deliberately.
; XOR's N/Z updates are replaced by OR before the first flag-dependent branch.

        MOV.W #$2000, r4
        XOR   r0, r0
        MOV.B #5, r1
        MOV.B #1, r3
        OR    r1, r1
        BEQ   done
loop:
        MOV.W [r4], r2
        ADD   r2, r0
        ADDP  #2, r4
        SUB   r3, r1
        BNE   loop
done:
        MOV.W r0, [$2100]
        HALT
