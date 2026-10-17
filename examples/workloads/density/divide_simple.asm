; Same ABI/status as divide.asm; small-input repeated-subtraction reference.
divide:
TST.W r1
BEQ divide_zero
MOV.W #0,r2
divide_loop:
CMP r1,r0
BCC divide_done
SUB r1,r0
ADDI.W #1,r2
BRA divide_loop
divide_done:
MOV r0,r1
MOV r2,r0
RET
divide_zero:
MOV.W #1,r0
MOV.W r0,[$3F00]
MOV.W #0,r0
MOV.W #0,r1
RET
