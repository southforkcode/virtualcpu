; Working ISA. Assemble as a flat image at $0100 in CS=0.
MOV.W #$2000,r4
MOV.W #$2100,r5
loop:
MOV.B [r4],r0
MOV.B r0,[r5]
TST.B r0
BEQ done
ADDP #1,r4
ADDP #1,r5
BRA loop
done:
HALT
