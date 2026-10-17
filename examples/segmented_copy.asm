; Working ISA. Assemble as a flat image at $0100 in CS=0.
MOV.B #3,r0
MOV r0,ds
MOV.W #$2000,r4
MOV.W #$2100,r5
MOV.W #$0302,r6
MOV.W [cs:r6-2],r2
ADDP #-2,r6
MOV.W r2,[ds:r5+8]
loop:
MOV.B [ds:r4],r0
MOV.B r0,[ds:r5]
TST.B r0
BEQ done
ADDP #1,r4
ADDP #1,r5
BRA loop
done:
HALT
