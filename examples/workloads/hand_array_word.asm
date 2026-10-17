; Same fixed five-element input and externally visible result as array_word.c.
CALL hand_sum
MOV.W r0,[$2F00]
SWI #1
HALT
hand_sum:
PUSH r4
MOV.W #$2000,r4
MOV.W #5,r2
MOV.W #0,r0
loop:
MOV.W [r4],r1
ADD r1,r0
ADDP #2,r4
SUBI.W #1,r2
BNE loop
POP r4
RET
