; Unsigned r0 / r1 -> quotient r0, remainder r1. B1 preserved.
; Explicit software status at DS:$3F00: 0 success, 1 zero divisor.
; NZCV/r2/r3 clobbered. All 16-bit inputs, including 17-bit partial remainder.
divide:
TST.W r1
BEQ divide_zero
PUSH r4
MOV r0,r2
MOV.W #0,r3
MOV.W #16,r4
divide_loop:
SHLI.W #1,r2
MOV.W #0,r0
ADC r3,r3
BCS divide_subtract
CMP r1,r3
BCC divide_next
divide_subtract:
SUB r1,r3
ORI.W #1,r2
divide_next:
MOV r4,r0
SUBI.W #1,r0
MOV r0,r4
BNE divide_loop
MOV r2,r0
MOV r3,r1
POP r4
RET
divide_zero:
MOV.W #1,r0
MOV.W r0,[$3F00]
MOV.W #0,r0
MOV.W #0,r1
RET
