; Working ISA. Assemble as a flat image at $0100 in CS=0.
MOV.B #5,r0
CALL fact
MOV.W r0,[$2000]
HALT
fact:
PUSH r0
CMPI.W #1,r0
BLS base
SUBI.W #1,r0
CALL fact
MOV r0,r1
MOV.W [sp+0],r2
MOV.B #0,r0
multiply:
ADD r1,r0
SUBI.W #1,r2
BNE multiply
POP r3
RET
base:
MOV.B #1,r0
POP r3
RET
