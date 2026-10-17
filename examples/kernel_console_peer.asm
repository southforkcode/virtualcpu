; B: private progress while A blocks; test modes exercise idle and cancellation.
MOV.W #$CAFE,r3
MOV.W #$BEEF,r5
loop:
MOV.W [$2000],r0
ADDI.W #1,r0
MOV.W r0,[$2000]
MOV.W [$2010],r0
CMPI.W #1,r0
BEQ block
CMPI.W #2,r0
BEQ cancel
SWI #5
CMPI.W #$CAFE,r3
BNE bad
MOV r5,r0
CMPI.W #$BEEF,r0
BNE bad
BRA loop
block:
MOV.W #32,r0
MOV.W #3,r1
MOV.W #$2100,r4
SWI #2
BRA loop
cancel:
SWI #3
MOV.W #0,r0
MOV.W r0,[$2010]
BRA loop
bad:
SWI #255
HALT
