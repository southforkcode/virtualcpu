; Test handler at 0:$1C00: conflicting diagnostic query must return BUSY.
MOV.W #$6080,r4
CALL $7023
CMPI.W #2,r0
BNE $1400
TST.W r1
BNE $1400
MOV.W #1,r0
MOV.W r0,[$50E0]
JS $1800
