; User CS=1:$0100 in maps 7/8, each with private code/data/stack backing.
MOV.W #$2000,r0
MOV r0,dp
MOV.W #5,r1
SWI #2
CMPI.W #1,r1
BNE bad
loop:
MOV.W [r4],r0
ADDI.W #1,r0
MOV.W r0,[r4]
SWI #1
BRA loop
bad:
SWI #255
BRA bad
