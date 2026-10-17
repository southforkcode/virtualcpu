; Signed wrapper, truncates toward zero, remainder sign follows dividend.
; status 2 = MIN/-1; no new CPU arithmetic exception is assigned here.
signed_divide:
PUSH r4
PUSH r5
MOV r0,r4
MOV r1,r5
CMPI.W #$8000,r0
BNE signed_abs
CMPI.W #$FFFF,r1
BEQ signed_overflow
signed_abs:
TST.W r0
BPL signed_divisor
MOV.W #0,r2
SUB r0,r2
MOV r2,r0
signed_divisor:
TST.W r1
BPL signed_magnitudes
MOV.W #0,r2
SUB r1,r2
MOV r2,r1
signed_magnitudes:
CALL divide
MOV r4,r2
MOV r5,r3
XOR r3,r2
TST.W r2
BPL signed_remainder
MOV.W #0,r2
SUB r0,r2
MOV r2,r0
signed_remainder:
MOV r4,r2
TST.W r2
BPL signed_done
MOV.W #0,r2
SUB r1,r2
MOV r2,r1
signed_done:
POP r5
POP r4
RET
signed_overflow:
MOV.W #2,r0
MOV.W r0,[$3F00]
MOV.W #0,r0
MOV.W #0,r1
BRA signed_done
