; Working ISA. Assemble as a flat image at $0100 in CS=0.
MOV.W #$2002,r4
MOV.W #$200A,r5
MOV.W #$2000,r6
outer:
MOV r4,r0
MOV r5,r1
CMP r1,r0
BCS done
MOV r0,dp
MOV.W [dp+0],r2
MOV r0,r3
inner:
MOV r6,r1
CMP r1,r3
BEQ place
MOV r3,dp
MOV.W [dp-2],r0
CMP r2,r0
BLE place
MOV.W r0,[dp+0]
SUBI.W #2,r3
BRA inner
place:
MOV r3,dp
MOV.W r2,[dp+0]
ADDP #2,r4
BRA outer
done:
HALT
