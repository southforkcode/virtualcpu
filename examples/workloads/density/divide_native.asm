; Software-compatible status shim around native unsigned quotient/remainder.
divide:
TST.W r1
BEQ divide_zero
MOV r0,r2
DIVU r1,r0
MODU r1,r2
MOV r2,r1
RET
divide_zero:
MOV.W #1,r0
MOV.W r0,[$3F00]
divide_error:
MOV.W #0,r0
MOV.W #0,r1
RET
