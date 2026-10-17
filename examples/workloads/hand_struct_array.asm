CALL hand_sum
MOV.W r0,[$2F00]
SWI #1
HALT
hand_sum:
PUSH r4
MOV.W #$2000,r4
MOV.W #3,r2
MOV.W #0,r0
loop:
MOV.W [r4+2],r1
ADD r1,r0
MOV.B [r4],r1
ADD r1,r0
ADDP #4,r4
SUBI.W #1,r2
BNE loop
MOV.W #9,r1
MOV.B r1,[$2004]
MOV.W [$2006],r1
MOV.W r1,[$200C]
POP r4
RET
