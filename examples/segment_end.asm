; Working ISA. Assemble as a flat image at $0100 in CS=0.
MOV.W #$FFFD,r4
MOV.B #3,r1
MOV.B #0,r2
loop:
MOV.B [r4],r0
ADD r0,r2
DEC.W r1
BEQ done
ADDP #1,r4
BRA loop
done:
MOV.W r2,[$2000]
HALT
