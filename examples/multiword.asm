; Working ISA. Assemble as a flat image at $0100 in CS=0.
MOV.W #$FFFF,r0
MOV.B #1,r1
MOV.B #1,r2
MOV.B #2,r3
ADD r2,r0
ADC r3,r1
MOV.W r0,[$2000]
MOV.W r1,[$2002]
HALT
