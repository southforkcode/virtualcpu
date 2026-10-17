; Working ISA. Assemble as a flat image at $0100 in CS=0.
MOV.B #0,r0
MOV.B #1,r1
MOV.B #10,r2
TST.W r2
BEQ done
loop:
MOV r0,r3
MOV r1,r0
ADD r3,r1
SUBI.W #1,r2
BNE loop
done:
MOV.W r0,[$2000]
HALT
