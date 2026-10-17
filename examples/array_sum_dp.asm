; Working ISA. Assemble as a flat image at $0100 in CS=0.
MOV.B #0,r0
MOV.B #5,r1
MOV.W #$2000,r2
MOV r2,dp
TST.W r1
BEQ done
loop:
MOV.W [dp+0],r2
ADD r2,r0
ADDP #2,dp
DEC.W r1
BNE loop
done:
MOV.W r0,[$2100]
HALT
